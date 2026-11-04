#!/usr/bin/env python3
"""Oscillator and sample-clock offset tolerance through the full stack (P4 item 2.1).

For each PHY and offset, one clean-arq scenario (bidirectional UDP, ARQ,
TDMA) runs with node 2's simulated oscillator off by PPM relative to node 1:
carrier offset PPM*fc on both links (opposite signs) and sample clock offset
PPM, as on a USRP whose LO and ADC share one reference (--sim-clock-ppm).
A second sweep sets only the sample clock offset on both links (--link
SFO_PPM, no carrier offset). Records per-direction PHY frame delivery and
application delivery. Simulation; high SNR (the scenario default), so only
the offsets limit reception.

Usage: sim-osc-sweep.py --workspace WS --outdir DIR [--jobs 3] [--rate 1e6]
"""
import argparse
import concurrent.futures
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PHYS = {'native': ['--phy', 'native-ofdm', '-c', 'rs8', '-k', 'none', '-m', 'qpsk'],
        'liquid': ['--phy', 'ofdm', '-c', 'rs8', '-k', 'none', '-m', 'qpsk']}


def point(args, phy, kind, fc, ppm):
    name = f'{phy}-{kind}-{fc/1e9:g}GHz-{ppm:+g}ppm'
    out = Path(args.outdir) / name
    if kind == 'osc':
        node_extra = ['--node-extra', '2', f'--sim-clock-ppm {ppm}']
    else:   # sample clock only, both links
        node_extra = ['--node-extra', '2', f'--link 1:0:0:0:0:0:{-ppm}', '--node-extra', '1', f'--link 2:0:0:0:0:0:{ppm}']
    cmd = ['bash', str(ROOT / 'grid' / 'run-sim-fullstack.sh'), args.workspace, str(out), '--scenario', 'clean-arq',
           '--bandwidth', str(args.rate), '--duration', str(args.duration)] + node_extra + ['--', '-f', str(fc)] + PHYS[phy]
    subprocess.run(cmd, stdout=open(str(out) + '.log', 'w'), stderr=subprocess.STDOUT)
    try:
        s = json.loads((out / 'clean-arq' / 'summary.json').read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {'name': name, 'phy': phy, 'kind': kind, 'fc': fc, 'ppm': ppm, 'error': 'no summary'}
    la = s.get('links_accounting', {})
    flows = {f['flow']: f['delivery'] for f in s.get('flows', [])}
    sub = args.rate/48   # subcarrier spacing at M = 48
    return {'name': name, 'phy': phy, 'kind': kind, 'fc': fc, 'ppm': ppm, 'rate': args.rate,
            # link 1->2 sees node 2's oscillator high relative to node 1 by ppm: CFO -ppm*fc at node 2's receiver
            'cfo_hz_1to2': -ppm*1e-6*fc if kind == 'osc' else 0.0, 'cfo_subcarriers_1to2': -ppm*1e-6*fc/sub if kind == 'osc' else 0.0,
            'frame_delivery_1to2': la.get('1->2', {}).get('frame_delivery'), 'frame_delivery_2to1': la.get('2->1', {}).get('frame_delivery'),
            'app_delivery_1to2': flows.get('1->2:5001'), 'app_delivery_2to1': flows.get('2->1:5002'),
            'retx_1to2': la.get('1->2', {}).get('retransmissions'), 'retx_2to1': la.get('2->1', {}).get('retransmissions'),
            'passed': s.get('passed')}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--workspace', required=True)
    ap.add_argument('--outdir', required=True)
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--rate', type=float, default=1e6)
    ap.add_argument('--duration', type=float, default=15.0)
    ap.add_argument('--osc-ppm', type=float, nargs='*', default=[0, 1, 2, 5, 10, 15, 20, 25, 30, 40, 50])
    ap.add_argument('--fc', type=float, nargs='*', default=[1e9, 2.4e9])
    ap.add_argument('--sfo-ppm', type=float, nargs='*', default=[20, 50, 100, 200, 500])
    args = ap.parse_args()
    Path(args.outdir).mkdir(parents=True, exist_ok=True)
    jobs = [(phy, 'osc', fc, ppm) for phy in PHYS for fc in args.fc for ppm in args.osc_ppm]
    jobs += [(phy, 'sfo', 1e9, ppm) for phy in PHYS for ppm in args.sfo_ppm]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda j: point(args, *j), jobs))
    (Path(args.outdir) / 'sweep.json').write_text(json.dumps(results, indent=1))
    for r in results:
        print(f"{r['name']:32s} frames 1->2 {r.get('frame_delivery_1to2')} 2->1 {r.get('frame_delivery_2to1')}  "
              f"app {r.get('app_delivery_1to2')} {r.get('app_delivery_2to1')}  {r.get('error', '')}")


if __name__ == '__main__':
    raise SystemExit(main())
