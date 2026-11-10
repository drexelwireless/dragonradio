#!/usr/bin/env python3
"""Table of M4 reaction-test results (docs/sensing-delay-measurement.rst).

Reads OUT from grid/sense-delay-m4-campaign.sh: for each cell, NAME.ping.json
from the pinging node A and NAME.react.json from the reacting node B, found
anywhere under OUT. D is measured on A's clock, from the start of A's own
burst to the start of B's reply. B's "plan" is its own estimate, from the
detected edge to the reply it asked for (timed replies) or to burstTX
returning (untimed), so D - plan is what B's estimate missed.

a = D p99 / T, with T one 8046-sample native frame (8 ms at 1 MS/s, 1.6 ms at
5 MS/s) and with an 8 ms aggregate.

Usage: sense-delay-m4-table.py OUT [--csv FILE]
"""
import argparse
import csv
import json
import pathlib

FRAME_SAMPS = 8046


def get(d, *ks):
    for k in ks:
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--csv')
    args = ap.parse_args()
    root = pathlib.Path(args.out)
    # Key by pair directory and cell name: the link-check cells have the
    # same name in every pair.
    reacts = {(p.parent.parent, p.name[:-len('.react.json')]): p for p in root.rglob('*.react.json')}
    rows = []
    for p in sorted(root.rglob('*.ping.json')):
        name = p.name[:-len('.ping.json')]
        a = json.loads(p.read_text())
        key = (p.parent.parent, name)
        b = json.loads(reacts[key].read_text()) if key in reacts else {}
        d = a.get('d_total_ms') or {}
        pings = a.get('pings') or 0
        got = pings - (a.get('own_missed') or 0) - (a.get('reply_missed') or 0)
        plan = b.get('react_planned_ms') or b.get('react_send_return_ms') or {}
        if b.get('lead_ms') == 0 and b.get('react_trigger_ms'):
            # Untimed: plan = edge to trigger + trigger to burstTX return.
            plan = {'p50': round((get(b, 'react_trigger_ms', 'p50') or 0)
                                 + (get(b, 'react_send_return_ms', 'p50') or 0), 4)}
        rate = a.get('rate') or 0
        p99 = d.get('p99')
        rows.append({
            'cell': name if not name.startswith('check') else f'{p.parent.parent.name}-{name}',
            'pings': pings,
            'replied': round(got/pings, 3) if pings else None,
            'D_p50': d.get('p50'), 'D_p90': d.get('p90'), 'D_p99': p99, 'D_max': d.get('max'),
            'B_plan_p50': plan.get('p50'),
            'D_minus_plan_p50': round(d['p50'] - plan['p50'], 4) if d.get('p50') is not None and plan.get('p50') is not None else None,
            'B_chunk_ms': b.get('chunk_ms'), 'B_rx_gaps': b.get('rx_gaps'),
            'B_tx_late': b.get('tx_late'), 'A_tx_late': a.get('tx_late'),
            'B_reacts': b.get('reacts'), 'A_edges': a.get('edges'),
            'a_frame': round(p99/(FRAME_SAMPS/rate*1e3), 3) if p99 and rate else None,
            'a_8ms': round(p99/8.0, 3) if p99 else None,
            'warning': a.get('warning', ''),
        })
    if not rows:
        print('no *.ping.json under', root)
        return 1
    cols = ['cell', 'pings', 'replied', 'D_p50', 'D_p99', 'D_max', 'B_plan_p50', 'D_minus_plan_p50',
            'B_chunk_ms', 'B_rx_gaps', 'B_tx_late', 'a_frame', 'a_8ms']
    w = {c: max(len(c), *(len(str(r[c])) for r in rows)) for c in cols}
    print('  '.join(c.ljust(w[c]) for c in cols))
    for r in rows:
        print('  '.join(str(r[c]).ljust(w[c]) for c in cols) + (f'  ! {r["warning"]}' if r['warning'] else ''))
    if args.csv:
        with open(args.csv, 'w', newline='') as f:
            wr = csv.DictWriter(f, fieldnames=list(rows[0]))
            wr.writeheader()
            wr.writerows(rows)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
