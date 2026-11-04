#!/usr/bin/env python3
"""Stratified summary of full-stack trials (OTA from ota-fullstack.py, or
simulation summaries carrying the same 'strata'), P4 item 2.4.

Trials are grouped by stratum: pair type, strength, placement, power, clock,
carrier map, PHY, bandwidth, frequency and build (plan section 7.7). Strata
are never pooled. Trials that are invalid for RF conclusions (any continuity
error: late/underflow TX, RX error, missed slot) are excluded and listed
with the reason, never silently dropped. Within each stratum, per-flow
delivery, UDP latency, TCP goodput and RTT get trial-level bootstrap 95%
intervals. Where native and liquid trials share every other stratum key and
a pair_block, their paired difference is reported.

Usage: summarize-fullstack.py TRIAL_DIR_OR_SUMMARY... --output OUT.json
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path

import numpy as np

STRATA = ('pair_type', 'strength', 'placement', 'power', 'clock', 'carrier_map', 'phy', 'bandwidth', 'frequency', 'build')


def load(paths):
    out = []
    for p in map(Path, paths):
        f = p / 'summary.json' if p.is_dir() else p
        s = json.loads(f.read_text())
        s['_path'] = str(f)
        out.append(s)
    return out


def trial_metrics(s):
    m = {}
    for f in s.get('flows', []):
        if f.get('delivery') is not None:
            m[f'udp {f["flow"]} delivery'] = f['delivery']
        if f.get('latency_p50_ms') is not None:
            m[f'udp {f["flow"]} latency p50 ms'] = f['latency_p50_ms']
    for b in s.get('tcp_bulk', []):
        if 'goodput_kbps' in b:
            m[f'tcp bulk {b["src"]}->{b["dst"]} goodput kb/s'] = b['goodput_kbps']
    for r in s.get('tcp_rr', []):
        if r.get('rtt_p50') is not None:
            m[f'tcp rr {r["src"]}->{r["dst"]} rtt p50 ms'] = 1e3*r['rtt_p50']
    return m


def interval(values, rng, n=10000):
    v = np.asarray(values, float)
    if len(v) == 1:
        return {'mean': float(v[0]), 'n': 1, 'bootstrap95': None}
    idx = rng.integers(0, len(v), (n, len(v)))
    return {'mean': float(v.mean()), 'n': int(len(v)), 'bootstrap95': [float(x) for x in np.quantile(v[idx].mean(axis=1), [0.025, 0.975])]}


def summarize(summaries, min_trials=3):
    rng = np.random.default_rng(2026)
    excluded, groups = [], defaultdict(list)
    for s in summaries:
        strata = s.get('strata')
        if not strata or any(k not in strata for k in STRATA):
            excluded.append({'trial': s['_path'], 'reason': f'missing strata keys {[k for k in STRATA if k not in (strata or {})]}'})
            continue
        if not s.get('valid_for_rf_conclusions', False):
            excluded.append({'trial': s['_path'], 'reason': f'continuity errors {s.get("continuity")}'})
            continue
        groups[tuple(str(strata[k]) for k in STRATA)].append(s)
    out = {'strata_keys': list(STRATA), 'excluded': excluded, 'strata': []}
    by_rest = defaultdict(dict)
    for key, trials in sorted(groups.items()):
        metrics = defaultdict(list)
        for t in trials:
            for k, v in trial_metrics(t).items():
                metrics[k].append(v)
        entry = {'stratum': dict(zip(STRATA, key)), 'trials': len(trials),
                 'enough_trials': len(trials) >= min_trials,
                 'passed': sum(bool(t.get('passed')) for t in trials),
                 'metrics': {k: interval(v, rng) for k, v in sorted(metrics.items())}}
        out['strata'].append(entry)
        rest = tuple(v for k, v in zip(STRATA, key) if k != 'phy')
        by_rest[rest][dict(zip(STRATA, key))['phy']] = trials
    # Paired native - liquid differences within otherwise identical strata.
    out['paired_native_minus_liquid'] = []
    for rest, phys in by_rest.items():
        if 'native-ofdm' not in phys or 'ofdm' not in phys:
            continue
        nat = {t['strata'].get('pair_block'): t for t in phys['native-ofdm'] if t['strata'].get('pair_block') is not None}
        liq = {t['strata'].get('pair_block'): t for t in phys['ofdm'] if t['strata'].get('pair_block') is not None}
        blocks = sorted(set(nat) & set(liq))
        diffs = defaultdict(list)
        for b in blocks:
            mn, ml = trial_metrics(nat[b]), trial_metrics(liq[b])
            for k in set(mn) & set(ml):
                diffs[k].append(mn[k] - ml[k])
        out['paired_native_minus_liquid'].append({
            'stratum': dict(zip([k for k in STRATA if k != 'phy'], rest)), 'pair_blocks': blocks,
            'differences': {k: interval(v, rng) for k, v in sorted(diffs.items())}})
    out['caveat'] = ('trial-level bootstrap within strata only; at least 3 (preferably 10) randomized paired blocks '
                     'per stratum; excluded trials and their causes must be reported with the results')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('trials', nargs='+')
    ap.add_argument('--output', required=True)
    ap.add_argument('--min-trials', type=int, default=3)
    args = ap.parse_args()
    out = summarize(load(args.trials), args.min_trials)
    Path(args.output).write_text(json.dumps(out, indent=1))
    print(json.dumps({'strata': len(out['strata']), 'excluded': len(out['excluded']),
                      'paired': len(out['paired_native_minus_liquid'])}))


if __name__ == '__main__':
    raise SystemExit(main())
