#!/usr/bin/env python3
"""Per-link radio metrics from the production HDF5 logs of a trial or simulation.

  link-metrics.py OUTDIR [OUTDIR ...] [--output FILE]

OUTDIR is an ota-fullstack trial directory or a sim-fullstack output
directory: both keep each node's log at OUTDIR/logs/node-NNN/radio.h5. For
every receiving node and transmitter it reports, from the "recv" dataset:

* frames heard (header valid or not, with --log-invalid-headers) and the
  header and payload CRC pass rates;
* EVM (dB), RSSI (dB) and CFO (fraction of the sample rate) over frames
  whose payload passed: median, 10th and 90th percentile.

From the "send" dataset of each node it reports packets sent, retransmissions
and drops, and, when the trial logged transmitted IQ (--log-iq), the average
and peak power of the transmitted samples in dBFS: the check that two PHYs
transmit at the same average power. Writes JSON (stdout or --output) and
prints a short table on stderr. Needs h5py and numpy.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np


def _q(values):
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    if not v.size:
        return None
    p10, p50, p90 = np.percentile(v, [10, 50, 90])
    return {'p10': round(float(p10), 3), 'median': round(float(p50), 3), 'p90': round(float(p90), 3), 'n': int(v.size)}


def delay_metrics(f, large=0.2):
    """Network-to-air delay of first transmissions of sequenced data, separating
    the start of traffic from steady state. settled_s: seconds from the first
    data packet until this delay stays under `large` seconds."""
    if 'send' not in f:
        return None
    s = f['send'][()]
    names = s.dtype.names
    if not {'net_timestamp', 'timestamp', 'nretrans', 'flags', 'data_len'} <= set(names):
        return None
    d = s[((s['flags'] & 4) != 0) & (s['nretrans'] == 0) & (s['data_len'] > 0) & (s['net_timestamp'] > 0)]
    if not len(d):
        return None
    t = d['net_timestamp'] - d['net_timestamp'].min()
    dl = d['timestamp'] - d['net_timestamp']
    bad = t[dl >= large]
    settled = float(bad.max()) if len(bad) else 0.0
    steady = dl[t > settled]
    return {'packets': int(len(d)), 'settled_s': round(settled, 2), 'max_s': round(float(dl.max()), 3),
            'steady_p50_ms': round(float(np.median(steady)) * 1e3, 1) if len(steady) else None,
            'steady_p95_ms': round(float(np.percentile(steady, 95)) * 1e3, 1) if len(steady) else None,
            'steady_seconds': round(float(t.max() - settled), 1)}


def node_metrics(path):
    import h5py  # pylint: disable=import-outside-toplevel

    out = {'log': str(path), 'rx': {}, 'tx': {}}
    with h5py.File(path, 'r') as f:
        out['delay'] = delay_metrics(f)
        if 'recv' in f:
            r = f['recv']
            names = r.dtype.names
            cols = {k: r[k] for k in ('src', 'header_valid', 'payload_valid', 'evm', 'rssi', 'cfo') if k in names}
            if 'src' in cols:
                for src in sorted(set(int(s) for s in cols['src'])):
                    sel = cols['src'] == src
                    hv = cols['header_valid'][sel].astype(bool)
                    pv = cols['payload_valid'][sel].astype(bool) & hv
                    entry = {'frames': int(sel.sum()), 'header_ok': int(hv.sum()), 'payload_ok': int(pv.sum()),
                             'header_ok_rate': round(float(hv.mean()), 4) if sel.any() else None,
                             'payload_ok_rate': round(float(pv.sum() / hv.sum()), 4) if hv.any() else None}
                    for k in ('evm', 'rssi', 'cfo'):
                        if k in cols:
                            entry[k] = _q(cols[k][sel][pv])
                    out['rx'][str(src)] = entry
            if 'header_valid' in cols:
                # Frames whose header failed carry no trustworthy source: counted
                # apart (interference such as radar bursts shows up here).
                out['rx_header_invalid'] = int((~cols['header_valid'].astype(bool)).sum())
        if 'send' in f:
            s = f['send']
            names = s.dtype.names
            tx = {'packets': int(s.shape[0])}
            if 'nretrans' in names:
                tx['retransmissions'] = int((s['nretrans'] > 0).sum())
            if 'dropped' in names:
                tx['dropped'] = int(s['dropped'].astype(bool).sum())
            if 'iq_data' in names and s.shape[0]:
                power, peak, total = 0.0, 0.0, 0
                for iq in s['iq_data']:
                    x = np.asarray(iq)
                    if x.dtype.names:            # stored as {real, imag} pairs
                        x = x[x.dtype.names[0]] + 1j * x[x.dtype.names[1]]
                    elif not np.iscomplexobj(x) and x.size % 2 == 0:
                        x = x.view(np.float32).astype(np.float64)
                        x = x[0::2] + 1j * x[1::2]
                    if x.size:
                        p = np.abs(x) ** 2
                        power += float(p.sum())
                        peak = max(peak, float(p.max()))
                        total += x.size
                if total:
                    tx['tx_power_dbfs'] = round(10 * np.log10(power / total), 2)
                    tx['tx_peak_dbfs'] = round(10 * np.log10(peak), 2)
                    tx['tx_samples'] = total
            out['tx'] = tx
    return out


def outdir_metrics(outdir):
    logs = sorted(Path(outdir).glob('logs/node-*/radio*.h5'))
    nodes = {}
    for path in logs:
        node = int(path.parent.name.split('-')[1])
        nodes.setdefault(str(node), []).append(node_metrics(path))
    return {'outdir': str(outdir), 'nodes': {k: v[0] if len(v) == 1 else v for k, v in nodes.items()}}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('outdirs', nargs='+', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args(argv)
    result = [outdir_metrics(d) for d in args.outdirs]
    text = json.dumps(result if len(result) > 1 else result[0], indent=1)
    if args.output:
        args.output.write_text(text + '\n')
    else:
        print(text)
    for r in result:
        for node, m in sorted(r['nodes'].items()):
            if isinstance(m, list):
                m = m[0]
            tx = m.get('tx', {})
            print(f'{r["outdir"]} node {node}: sent {tx.get("packets")}'
                  + (f', TX {tx["tx_power_dbfs"]} dBFS avg / {tx["tx_peak_dbfs"]} peak' if 'tx_power_dbfs' in tx else ''),
                  file=sys.stderr)
            for src, e in sorted(m['rx'].items()):
                evm = (e.get('evm') or {}).get('median')
                rssi = (e.get('rssi') or {}).get('median')
                print(f'  from {src}: {e["frames"]} frames, header {e["header_ok_rate"]}, payload {e["payload_ok_rate"]},'
                      f' EVM {evm} dB, RSSI {rssi} dB', file=sys.stderr)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
