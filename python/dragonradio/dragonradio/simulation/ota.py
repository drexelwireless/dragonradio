"""Reproducible, finite, capture-first PHY experiments; no MAC or ARQ claims."""
import hashlib
import json
import math
from pathlib import Path
import time
import uuid
from dataclasses import replace

import numpy as np

from .frame import FrameEngine, ModulatedFrame
from .ofdma import OFDMAConfig, Packet, ofdm_config

MODES = ('legacy-ofdm', 'native-ofdm', 'native-ofdma')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def save_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def configuration(mode, user, sample_rate, allocation_swap=False):
    if mode != 'native-ofdma':
        return ofdm_config(user_id=user, sample_rate=sample_rate)
    cfg = OFDMAConfig(sample_rate=sample_rate)
    if allocation_swap:
        a, b = cfg.allocations
        cfg = replace(cfg, allocations=(replace(b, user_id=1), replace(a, user_id=2)))
    return cfg


def prepare(directory, frames=100, sample_rate=1e6, seed=2026, rms=0.08, modes=MODES, allocation_swap=False):
    """Equal payloads per user; alternate full-band OFDM vs shared OFDMA slots.

    Normalize active samples to the same composite RMS, rejecting peak clipping.
    Offered load is saturated. Report airtime rather than pretending slots match.
    """
    directory = Path(directory)
    if directory.exists():
        raise ValueError('use a fresh campaign directory to preserve provenance')
    if (not 1 <= frames <= 500 or not 0 < rms <= .2 or
            not 1e5 <= sample_rate <= 5e6 or not modes or not set(modes) <= set(MODES)):
        raise ValueError('invalid bounded campaign parameters')
    directory.mkdir(parents=True)
    run_id = str(uuid.uuid4())
    rng = np.random.default_rng(seed)
    tag = uuid.UUID(run_id).bytes
    packets = {(user, i): Packet(0, user, i, tag + rng.bytes(240))
               for i in range(frames) for user in (1, 2)}
    manifest = dict(schema=1, run_id=run_id, seed=seed, frames_per_user=frames,
                    sample_rate=sample_rate, payload_bytes=256, gap_samples=64,
                    coding='uncoded QPSK + CRC32', scope='PHY only; no MAC/ARQ',
                    allocation_swap=allocation_swap,
                    expected=[dict(user=u, sequence=i, sha256=hashlib.sha256(p.payload).hexdigest())
                              for (u, i), p in packets.items()], waveforms={})
    for mode in modes:
        if mode == 'legacy-ofdm':
            from .liquid import LiquidOFDM
            modem = LiquidOFDM(ofdm_config(sample_rate=sample_rate))
            build = lambda ps: modem.modulate(ps[0])
        else:
            configs = {u: configuration(mode, u, sample_rate, allocation_swap) for u in (1, 2)}
            engines = {u: FrameEngine(c, 'native') for u, c in configs.items()}
            build = lambda ps: engines[ps[0].destination].build(ps).samples
        chunks, records, offset, active_energy, active_count = [], [], 0, 0., 0
        for i in range(frames):
            groups = [[packets[1, i], packets[2, i]]] if mode == 'native-ofdma' else [[packets[u, i]] for u in (1, 2)]
            for group in groups:
                samples = build(group)
                records.extend(dict(user=p.destination, sequence=i, start=offset, count=len(samples)) for p in group)
                chunks.extend((samples, np.zeros(64, dtype=np.complex64)))
                offset += len(samples) + 64
                active_energy += float(np.sum(np.abs(samples.astype(np.complex128)) ** 2))
                active_count += len(samples)
        # Quiet prefix/suffix simplify RX arming and end-of-capture checks.
        quiet = int(sample_rate * .05)
        waveform = np.concatenate((np.zeros(quiet, np.complex64), *chunks, np.zeros(quiet, np.complex64)))
        scale = rms / math.sqrt(active_energy / active_count)
        waveform = np.asarray(waveform * scale, dtype='<c8')
        peak = float(np.max(np.abs(waveform)))
        if peak > .5 or len(waveform) / sample_rate > 10:
            raise ValueError('waveform exceeds peak .5 or duration 10 seconds; reduce frames/RMS')
        for record in records:
            record['start'] += quiet
        path = directory / (mode + '.cf32')
        waveform.tofile(path)
        manifest['waveforms'][mode] = dict(file=path.name, sha256=digest(path), samples=len(waveform),
            duration_seconds=len(waveform)/sample_rate, traffic_seconds=(offset-64)/sample_rate,
            active_rms=rms, peak=peak, papr_db=20*math.log10(peak/rms), records=records)
    # Bind source and compiled libraries as actually used (uncommitted work too).
    root = Path(__file__).resolve().parents[4]
    manifest['source_sha256'] = {str(p.relative_to(root)): digest(p) for base in
        ('python/dragonradio/dragonradio/simulation', 'src/simulation', 'src/liquid')
        for p in (root/base).rglob('*') if p.is_file() and p.suffix in ('.py', '.cc', '.hh')}
    for name in ('scripts/ota-phy.py', 'scripts/summarize-ota.py', 'grid/run-trial.py'):
        manifest['source_sha256'][name] = digest(root/name)
    manifest['library_sha256'] = {str(p.relative_to(root)): digest(p) for base in ('build/phy', 'build/modem')
        for p in (root/base).glob('libdragonradio*') if p.suffix in ('.so', '.dylib')}
    save_json(directory/'manifest.json', manifest)
    return manifest


def verify_runtime(manifest):
    """A capture must use the same code and binaries as its simulation gate."""
    root = Path(__file__).resolve().parents[4]
    for group in ('source_sha256', 'library_sha256'):
        for relative, expected in manifest[group].items():
            if digest(root/relative) != expected:
                raise ValueError('runtime differs from prepared campaign: '+relative)


def load_wave(directory, mode):
    directory = Path(directory)
    manifest = json.loads((directory/'manifest.json').read_text())
    item = manifest['waveforms'][mode]
    path = directory/item['file']
    if digest(path) != item['sha256'] or path.stat().st_size != item['samples']*8:
        raise ValueError('waveform hash/size mismatch')
    return manifest, np.fromfile(path, dtype='<c8')


def decode(samples, mode, user, sample_rate, allocation_swap=False):
    """Decode without expected packet data; verification happens afterward."""
    start = time.process_time()
    packets, failed, timing = [], 0, []
    if mode == 'legacy-ofdm':
        from .liquid import LiquidOFDM
        receiver = LiquidOFDM(ofdm_config(sample_rate=sample_rate))
    else:
        cfg = configuration(mode, user, sample_rate, allocation_swap)
        receiver = FrameEngine(cfg, 'native').stream_receiver(user)
    init_cpu = time.process_time()-start
    start = time.process_time()
    for offset in range(0, len(samples), 4096):
        block = samples[offset:offset+4096]
        if mode == 'legacy-ofdm':
            packets.extend(p for p in receiver.receive_many(block) if p.destination == user)
        else:
            results = receiver.feed(block)
            packets.extend(r.packet for r in results if r.ok)
            timing.extend(dict(sequence=r.packet.sequence, sample_offset=r.start_sample,
                               evm_db=r.evm_db, cfo_subcarriers=r.cfo_subcarriers)
                          for r in results if r.ok)
            failed += sum(not r.ok for r in results)
    if mode != 'legacy-ofdm':
        results = receiver.feed([], final=True)
        packets.extend(r.packet for r in results if r.ok)
        timing.extend(dict(sequence=r.packet.sequence, sample_offset=r.start_sample,
                           evm_db=r.evm_db, cfo_subcarriers=r.cfo_subcarriers) for r in results if r.ok)
        failed += sum(not r.ok for r in results)
    cpu = time.process_time()-start
    return dict(packets=[dict(source=p.source, user=p.destination, sequence=p.sequence,
                              sha256=hashlib.sha256(p.payload).hexdigest()) for p in packets],
                acquired_failures=failed, decode_cpu_seconds=cpu, init_cpu_seconds=init_cpu,
                timing=timing, sample_count=len(samples))


def wilson(success, count):
    if not count:
        return [0., 1.]
    z, p = 1.95996398454, success/count
    den = 1+z*z/count
    mid = (p+z*z/(2*count))/den
    half = z*math.sqrt(p*(1-p)/count+z*z/(4*count*count))/den
    return [max(0., mid-half), min(1., mid+half)]


def evaluate(manifest, mode, reports):
    expected = {(p['user'], p['sequence']): p['sha256'] for p in manifest['expected']}
    users = {}
    for user in (1, 2):
        r = reports[user]
        seen, duplicates, invalid = set(), 0, 0
        for p in r['packets']:
            key = (p['user'], p['sequence'])
            if p['source'] != 0 or p['user'] != user or expected.get(key) != p['sha256']:
                invalid += 1
            elif key in seen:
                duplicates += 1
            else:
                seen.add(key)
        n, k = manifest['frames_per_user'], len(seen)
        users[user] = dict(delivered=k, expected=n, pdr=k/n, pdr_wilson95=wilson(k, n),
                          duplicates=duplicates, invalid=invalid,
                          goodput_bps=k*manifest['payload_bytes']*8/manifest['waveforms'][mode]['traffic_seconds'],
                          decode_cpu_seconds=r['decode_cpu_seconds'])
    rates = [u['goodput_bps'] for u in users.values()]
    fairness = sum(rates)**2/(2*sum(x*x for x in rates)) if any(rates) else 0.
    return dict(run_id=manifest['run_id'], mode=mode, users=users, jain_fairness=fairness,
                aggregate_goodput_bps=sum(rates), all_payloads_verified=all(
                    u['delivered'] == u['expected'] and not u['invalid'] for u in users.values()),
                power_comparison='equal composite digital active RMS; calibrate RF power externally',
                latency='not measured; no synchronized cross-host timestamp assumption')
