#!/usr/bin/env dragonradio
"""Host sensing-delay probe (docs/sensing-delay-measurement.rst).

Run with the radio executable (embedded Python). Modes:

  rx     Receive only. Measures sample arrival delay per receive chunk (M2).
  react  Reply with a short burst to every rising edge of received energy.
  ping   Send a short burst every --period and record received energy edges.
         Against a react node, the gap between this node's own burst and the
         reply, both heard on this node's receiver, is the end-to-end sensing
         delay on one clock (M4). Alone, it counts late timed bursts (M3).

Simulation (M1), with a relay from scripts/sim-air.py:

  dragonradio scripts/sense-delay.py --sim-air /tmp/air.sock --node-id 1 --mode react ...
  dragonradio scripts/sense-delay.py --sim-air /tmp/air.sock --node-id 2 --mode ping \\
      --sim-tx-leakage-db -10 ...

Hardware: --addr addr=192.168.10.2. Modes that transmit need --tx-ok, which
stands for Kapil's go-ahead with an RF approval reference; the TX gain is
capped at 20 dB.

Prints a JSON summary; --out writes it, --raw writes every measurement.
"""
import argparse
import bisect
import json
import math
import os
import resource
import sys

from _dragonradio.radio import USRP, SimUSRP, runSenseDelay  # pylint: disable=no-name-in-module

TX_GAIN_CAP_DB = 20.0


def percentiles(xs, ps=(0, 50, 90, 99, 100)):
    """Nearest-rank percentiles of xs, in milliseconds when scale=1e3."""
    xs = sorted(x for x in xs if x is not None and math.isfinite(x))
    if not xs:
        return None
    out = {'n': len(xs), 'mean': sum(xs)/len(xs)}
    for p in ps:
        k = min(len(xs) - 1, max(0, math.ceil(p/100*len(xs)) - 1))
        out[{0: 'min', 100: 'max'}.get(p, f'p{p}')] = xs[k]
    return out


def ms(stats):
    if stats is None:
        return None
    return {k: (v if k == 'n' else round(v*1e3, 4)) for k, v in stats.items()}


class Offset:
    """Device minus host clock offset, linear between calibration points."""

    def __init__(self, hosts, offsets):
        pts = sorted(zip(hosts, offsets))
        self.h = [p[0] for p in pts]
        self.o = [p[1] for p in pts]

    def __call__(self, h):
        if len(self.h) == 1:
            return self.o[0]
        i = bisect.bisect_left(self.h, h)
        i = min(max(i, 1), len(self.h) - 1)
        h0, h1 = self.h[i - 1], self.h[i]
        o0, o1 = self.o[i - 1], self.o[i]
        return o0 + (o1 - o0)*(h - h0)/(h1 - h0) if h1 > h0 else o0

    def drift_ppm(self):
        if len(self.h) < 2 or self.h[-1] <= self.h[0]:
            return None
        return (self.o[-1] - self.o[0])/(self.h[-1] - self.h[0])*1e6


def analyze(r, args):
    fs = r['rx_rate']
    off = Offset(r['cal_host'], r['cal_offset'])
    s = {
        'mode': args.mode,
        'device': 'sim' if args.sim_air else args.addr,
        'rate': fs,
        'rx_max_samps': r['chunk_samps'],
        'chunk_ms': round(r['chunk_samps']/fs*1e3, 4) if fs else None,
        'window': args.window,
        'window_ms': round(args.window/fs*1e3, 4) if fs else None,
        'lead_ms': args.lead*1e3,
        'noise_power_db': round(10*math.log10(r['noise_power']), 2) if r['noise_power'] > 0 else None,
        'threshold_db_over_noise': args.threshold_db,
        'chunks': len(r['chunk_n']),
        'rx_errors': r['rx_errors'],
        'tx_late': r['tx_late'],
        'tx_underflow': r['tx_underflow'],
        'cal_rtt_ms': ms(percentiles(r['cal_rtt'])),
        'clock_drift_ppm': off.drift_ppm(),
        'edges': len(r['edge_dev']),
    }

    # Sample arrival delay: host time when burstRX returned, mapped to device
    # time, minus device time just after the chunk's last sample.
    d_rx = [h + off(h) - e for h, e in zip(r['chunk_host'], r['chunk_dev_end'])]
    s['d_rx_ms'] = ms(percentiles(d_rx))

    # Gaps between chunks are overflows (samples the host never saw).
    gaps = 0
    gap_samps = 0
    for i in range(1, len(r['chunk_n'])):
        start = r['chunk_dev_end'][i] - r['chunk_n'][i]/fs
        g = round((start - r['chunk_dev_end'][i - 1])*fs)
        if g > 0:
            gaps += 1
            gap_samps += g
    s['rx_gaps'] = gaps
    s['rx_gap_samples'] = gap_samps

    # Edge to host: when the detector saw an edge, relative to the edge
    # window's end (D_rx plus waiting for the rest of the chunk).
    w = args.window/fs
    s['edge_seen_ms'] = ms(percentiles([h + off(h) - (e + w) for h, e in zip(r['edge_host'], r['edge_dev'])]))

    if args.mode == 'react':
        s['reacts'] = len(r['react_edge_dev'])
        timed = [t - e for t, e in zip(r['react_tx_dev'], r['react_edge_dev']) if math.isfinite(t)]
        # Planned: from the start of the edge window to the requested TX time.
        s['react_planned_ms'] = ms(percentiles(timed))
        s['react_trigger_ms'] = ms(percentiles([h + off(h) - e for h, e in
                                                 zip(r['react_host_trigger'], r['react_edge_dev'])]))
        s['react_send_return_ms'] = ms(percentiles([b - a for a, b in
                                                     zip(r['react_host_trigger'], r['react_host_sent'])]))

    if args.mode == 'ping':
        L = args.burst_len/fs
        edges = r['edge_dev']
        own_off, gaps_d = [], []
        missed_own = missed_reply = 0
        for tp, hs in zip(r['ping_tx_dev'], r['ping_host_sent']):
            # Timed: the own burst must start at the requested time (within
            # a window or so). Untimed: it starts some time after burstTX
            # returned, so look up to most of a period; the offset is then
            # the untimed send-to-air delay.
            span = L
            if not math.isfinite(tp):
                tp = hs + off(hs)
                span = 0.5*args.period
            i = bisect.bisect_left(edges, tp - 2*w)
            if i >= len(edges) or edges[i] > tp + span:
                missed_own += 1
                continue
            e1 = edges[i]
            own_off.append(e1 - tp)
            j = bisect.bisect_right(edges, e1 + L)
            if j >= len(edges) or edges[j] > tp + 0.9*args.period:
                missed_reply += 1
                continue
            gaps_d.append(edges[j] - e1)
        s['pings'] = len(r['ping_tx_dev'])
        s['late_fraction'] = round(r['tx_late']/len(r['ping_tx_dev']), 4) if r['ping_tx_dev'] else None
        s['own_missed'] = missed_own
        s['reply_missed'] = missed_reply
        # Own edge against the requested TX time: checks timed TX and the
        # detector (should be within about one window). For untimed bursts,
        # against the time burstTX returned: the send-to-air delay.
        s['own_edge_offset_ms'] = ms(percentiles(own_off))
        # End-to-end sensing delay D: start of own burst to start of reply.
        s['d_total_ms'] = ms(percentiles(gaps_d))
        if gaps_d and min(gaps_d) <= L + w:
            s['warning'] = 'some replies start within one burst of the ping; shorten --burst-len'
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    dev = ap.add_mutually_exclusive_group(required=True)
    dev.add_argument('--addr', help='UHD device address (hardware)')
    dev.add_argument('--sim-air', help='air relay socket (simulation)')
    ap.add_argument('--node-id', type=int, default=1, help='simulated node ID')
    ap.add_argument('--sim-rx-latency', type=float, default=2e-3)
    ap.add_argument('--sim-noise-db', type=float, default=-40.0)
    ap.add_argument('--sim-tx-leakage-db', type=float, default=None,
                    help='couple own TX into own RX at this gain (ping node in simulation)')
    ap.add_argument('--sim-seed', type=int, default=1)

    ap.add_argument('--mode', choices=['rx', 'react', 'ping'], default='rx')
    ap.add_argument('--duration', type=float, default=10.0)
    ap.add_argument('--rate', type=float, default=1e6)
    ap.add_argument('--freq', type=float, default=1.322e9)
    ap.add_argument('--tx-gain', type=float, default=0.0)
    ap.add_argument('--rx-gain', type=float, default=10.0)
    ap.add_argument('--tx-antenna', default='TX/RX')
    ap.add_argument('--rx-antenna', default='RX2')
    ap.add_argument('--tx-subdev')
    ap.add_argument('--rx-subdev')
    ap.add_argument('--rx-max-samps-factor', type=int, default=8)
    ap.add_argument('--window', type=int, default=32, help='energy window (samples)')
    ap.add_argument('--threshold-db', type=float, default=10.0)
    ap.add_argument('--noise-secs', type=float, default=0.5)
    ap.add_argument('--lead', type=float, default=5e-3, help='timed TX lead (s); 0 sends react bursts untimed')
    ap.add_argument('--period', type=float, default=0.05)
    ap.add_argument('--holdoff', type=float, default=None, help='react: ignore edges after a reply (s)')
    ap.add_argument('--burst-len', type=int, default=128)
    ap.add_argument('--amplitude', type=float, default=0.5)
    ap.add_argument('--tx-ok', action='store_true',
                    help='hardware: transmitting is approved (RF go-ahead with an approval reference)')
    ap.add_argument('--out', help='write the JSON summary here')
    ap.add_argument('--raw', help='write every measurement (JSON) here')
    # Some builds of the radio executable pass its own argv through, so
    # sys.argv[1] is this script; skip it.
    argv = sys.argv[1:]
    if argv and argv[0].endswith('sense-delay.py'):
        argv = argv[1:]
    args = ap.parse_args(argv)

    transmits = args.mode in ('react', 'ping')
    if args.addr and transmits:
        if not args.tx_ok:
            ap.error(f'--mode {args.mode} transmits; pass --tx-ok only with an RF go-ahead')
        if args.tx_gain > TX_GAIN_CAP_DB:
            ap.error(f'--tx-gain {args.tx_gain} exceeds the approved cap of {TX_GAIN_CAP_DB} dB')

    if args.sim_air:
        usrp = SimUSRP(args.sim_air, args.node_id, args.freq,
                       rx_latency=args.sim_rx_latency, seed=args.sim_seed)
        usrp.noise_power = args.sim_noise_db
        if args.sim_tx_leakage_db is not None:
            usrp.setImpairments(tx_leakage_db=args.sim_tx_leakage_db)
    else:
        usrp = USRP(args.addr, args.tx_subdev, args.rx_subdev, args.freq,
                    args.tx_antenna, args.rx_antenna,
                    args.tx_gain if transmits else 0.0, args.rx_gain)
    usrp.rx_rate = args.rate
    usrp.tx_rate = args.rate
    usrp.rx_max_samps_factor = args.rx_max_samps_factor
    usrp.tx_max_samps_factor = 8

    chunk = usrp.rx_max_samps/usrp.rx_rate
    if args.holdoff is None:
        # Long enough that a react node ignores its own reply as heard
        # through its own receiver.
        args.holdoff = max(0.025, args.lead + 3*chunk + args.burst_len/args.rate + 0.005)
    if args.mode == 'ping' and args.period < 2*args.holdoff:
        print(f'note: --period {args.period} is short against a react holdoff of about {args.holdoff:.3f} s',
              file=sys.stderr)

    ru0 = resource.getrusage(resource.RUSAGE_SELF)
    r = runSenseDelay(usrp, mode=args.mode, duration=args.duration, window=args.window,
                      threshold_db=args.threshold_db, noise_secs=args.noise_secs,
                      lead=args.lead, period=args.period, holdoff=args.holdoff,
                      burst_len=args.burst_len, amplitude=args.amplitude)
    ru1 = resource.getrusage(resource.RUSAGE_SELF)
    s = analyze(r, args)
    # Process CPU during the run, as a share of one core, and host load.
    cpu = (ru1.ru_utime - ru0.ru_utime) + (ru1.ru_stime - ru0.ru_stime)
    s['cpu_one_core'] = round(cpu/args.duration, 3)
    s['host_loadavg'] = os.getloadavg()
    s['host_cpus'] = os.cpu_count()
    s['holdoff_ms'] = round(args.holdoff*1e3, 3)
    if args.sim_air:
        s['sim_rx_latency_ms'] = args.sim_rx_latency*1e3
        s['sim_stats'] = dict(usrp.stats)
    text = json.dumps(s, indent=2)
    print(text)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(text + '\n')
    if args.raw:
        with open(args.raw, 'w') as f:
            json.dump(r, f)
    if hasattr(usrp, 'stop'):
        usrp.stop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
