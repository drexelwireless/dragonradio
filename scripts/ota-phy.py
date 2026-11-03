#!/usr/bin/env python3
"""Prepare, simulate, capture, and compare finite DragonRadio PHY experiments."""
import argparse
from dataclasses import asdict
import json
from pathlib import Path
import platform
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'python/dragonradio'))
import numpy as np
from dragonradio.simulation.ota import MODES, prepare, load_wave, decode, evaluate, save_json, digest, verify_runtime
from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.frame import ModulatedFrame
from dragonradio.simulation.transport import SDRConfig, UHDTransport


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'simulate', 'rx', 'tx', 'analyze', 'preflight'))
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--mode', choices=MODES, default='native-ofdma')
    parser.add_argument('--frames', type=int, default=100)
    parser.add_argument('--sample-rate', type=float, default=1e6)
    parser.add_argument('--rms', type=float, default=.08)
    parser.add_argument('--seed', type=int, default=2026)
    parser.add_argument('--snr', type=float, default=30)
    parser.add_argument('--cfo', type=float, default=.1, help='simulation CFO in subcarrier spacings')
    parser.add_argument('--swap', action='store_true', help='prepare OFDMA with user subbands exchanged')
    parser.add_argument('--user', type=int, choices=(1, 2), default=1)
    parser.add_argument('--sdr-config', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--duration', type=float, default=8)
    parser.add_argument('--ready', type=Path, help='RX readiness file on shared storage')
    parser.add_argument('--require-ready', type=Path, nargs=2, help='TX requires both fresh RX readiness files')
    parser.add_argument('--execute', action='store_true', help='Open hardware; TX requires approved RF parameters')
    args = parser.parse_args(argv)
    directory = args.campaign
    if args.action == 'prepare':
        report = prepare(directory, args.frames, args.sample_rate, args.seed, args.rms, allocation_swap=args.swap)
        print(json.dumps(dict(run_id=report['run_id'], modes=list(report['waveforms'])), indent=2))
        return 0
    manifest, samples = load_wave(directory, args.mode)
    verify_runtime(manifest)
    manifest_hash = digest(directory/'manifest.json')
    if args.action == 'simulate':
        summary = []
        for mode in manifest['waveforms']:
            _, waveform = load_wave(directory, mode)
            reports = {}
            for user in (1, 2):
                # Positive CFO exercises all backends; negative CFO is a separate stress test.
                channel = Channel(ChannelConfig(args.snr, (1, .15j), 197+user*13, args.cfo), args.seed+user)
                reports[user] = decode(channel.propagate(waveform, 128), mode, user, manifest['sample_rate'], manifest['allocation_swap'])
                save_json(directory/f'sim-{mode}-u{user}.json', reports[user])
            summary.append(dict(evaluate(manifest, mode, reports), simulation=dict(snr_db=args.snr,
                cfo_subcarriers=args.cfo, taps=['1', '0.15j'], seed=args.seed, hardware_opened=False)))
        save_json(args.output or directory/'simulation.json', summary)
        print(json.dumps(summary, indent=2))
        return 0 if all(r['all_payloads_verified'] for r in summary) else 1
    if args.action == 'analyze':
        reports = {}
        captures = {}
        for user in (1, 2):
            path = directory/f'{args.mode}-u{user}.cf32'
            meta = json.loads(path.with_suffix('.json').read_text())
            captures[user] = meta
            if (not meta['complete'] or meta['run_id'] != manifest['run_id'] or
                meta['manifest_sha256'] != manifest_hash or meta['mode'] != args.mode or
                meta['user'] != user or meta['sha256'] != digest(path) or
                meta['sdr']['sample_rate'] != manifest['sample_rate']):
                raise ValueError('capture provenance/continuity check failed')
            reports[user] = decode(np.memmap(path, dtype='<c8', mode='r'), args.mode, user, manifest['sample_rate'], manifest['allocation_swap'])
            save_json(directory/f'{args.mode}-u{user}-decoded.json', reports[user])
        report = evaluate(manifest, args.mode, reports)
        # A valid capture alone cannot prove a TX burst reached the antenna.
        tx = json.loads((directory/f'{args.mode}-tx.json').read_text())
        if (tx['manifest_sha256'] != manifest_hash or not tx['complete'] or
                tx['mode'] != args.mode or tx['run_id'] != manifest['run_id']):
            raise ValueError('missing or mismatched TX completion')
        report['tx'] = tx
        report['rx'] = captures
        save_json(args.output or directory/f'{args.mode}-result.json', report)
        print(json.dumps(report, indent=2))
        return 0  # RF packet loss is a measurement, not a tool failure.
    if not args.sdr_config:
        parser.error('hardware plan requires --sdr-config')
    cfg = SDRConfig(**json.loads(args.sdr_config.read_text()))
    if cfg.sample_rate != manifest['sample_rate']:
        raise ValueError('rate differs from prepared waveform')
    if not np.isfinite(args.duration) or not 0 < args.duration <= 30:
        raise ValueError('capture duration must be 0..30 seconds')
    if cfg.gain_db > 20 or cfg.gain_db < 0:
        raise ValueError('runner gain cap is 0..20 dB; use the approved lower limit')
    report = dict(action=args.action, run_id=manifest['run_id'], manifest_sha256=manifest_hash,
                  mode=args.mode, user=args.user, sdr=asdict(cfg), hardware_opened=False,
                  host=platform.node(), python=sys.version, complete=False)
    if not args.execute:
        print(json.dumps(report, indent=2))
        return 0
    role = 'tx' if args.action == 'tx' else 'rx'
    if args.action == 'tx':
        if not args.require_ready:
            raise ValueError('TX requires --require-ready RX1.json RX2.json')
        ready = [json.loads(p.read_text()) for p in args.require_ready]
        if ({r['user'] for r in ready} != {1, 2} or len({r['host'] for r in ready}) != 2 or any(
            r['run_id'] != manifest['run_id'] or r['mode'] != args.mode or
            r['manifest_sha256'] != manifest_hash or not 0 <= time.time()-r['unix_time'] < 60 or
            r['expires_unix']-time.time() < manifest['waveforms'][args.mode]['duration_seconds']+1 or
            any(r['sdr'][key] != asdict(cfg)[key] for key in ('sample_rate','center_frequency')) for r in ready)):
            raise ValueError('RX readiness mismatch, stale, or too little capture time remains')
    transport = UHDTransport(cfg, role)
    report['hardware_opened'] = True
    # Readbacks, not requested values alone, accompany every hardware action.
    report['uhd_version'] = str(getattr(transport.uhd, 'get_version_string', lambda: 'unknown')())
    report['device'] = str(transport.usrp.get_usrp_rx_info() if role == 'rx' else transport.usrp.get_usrp_tx_info())
    report['actual'] = {key: getattr(transport.usrp, f'get_{role}_{key}')(cfg.channel) for key in ('rate', 'freq', 'gain', 'antenna')}
    actual = report['actual']
    if (abs(actual['freq']-cfg.center_frequency) > cfg.sample_rate/128/10 or
            abs(actual['gain']-cfg.gain_db) > .5 or actual['gain'] > 20):
        raise RuntimeError('hardware tuning/gain readback differs from approved configuration')
    if args.action == 'preflight':
        report['complete'] = True
        print(json.dumps(report, indent=2))
        if args.output:
            save_json(args.output, report)
        return 0
    output = args.output or directory/(f'{args.mode}-tx.json' if role == 'tx' else f'{args.mode}-u{args.user}.cf32')
    if output.exists():
        raise ValueError('refusing to overwrite experiment evidence')
    started = time.time()
    try:
        if role == 'tx':
            report['result'] = transport.transmit(ModulatedFrame(samples, (), cfg.sample_rate, args.mode), preserve_scale=True)
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            partial = output.with_suffix('.partial')
            class Sink:
                count = 0
                def feed(self, block, final=False):
                    np.asarray(block, dtype='<c8').tofile(stream)
                    self.count += len(block)
                    return []
                def reset(self):
                    pass
            sink = Sink()
            def ready(md):
                report['first_sample_device_seconds'] = md.time_spec.get_real_secs() if md.has_time_spec else None
                if args.ready:
                    save_json(args.ready, dict(run_id=manifest['run_id'], manifest_sha256=manifest_hash,
                        mode=args.mode, user=args.user, host=platform.node(), sdr=asdict(cfg),
                        unix_time=time.time(), expires_unix=started+args.duration))
            with partial.open('xb') as stream:
                transport.receive(sink, args.duration, on_ready=ready)
            partial.replace(output)
            report['samples'] = sink.count
            report['sha256'] = digest(output)
            if sink.count < cfg.sample_rate*args.duration*.9:
                raise RuntimeError('capture contains less than 90% of requested duration')
        report['complete'] = True
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        report['unix_start'], report['unix_end'] = started, time.time()
        save_json(output if role == 'tx' else output.with_suffix('.json'), report)
        if role == 'rx' and args.ready:
            args.ready.unlink(missing_ok=True)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError, KeyError) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(2)
