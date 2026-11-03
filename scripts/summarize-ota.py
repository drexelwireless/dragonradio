#!/usr/bin/env python3
"""Paired-trial OTA comparison, with trial-level bootstrap uncertainty."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'python/dragonradio'))
import numpy as np
from dragonradio.simulation.ota import MODES, save_json


def summarize(directories, margin=.01):
    if len(directories) < 3:
        raise ValueError('at least three complete independent paired trials required')
    trials, identities, baseline, baseline_rf = [], set(), None, None
    for directory in map(Path, directories):
        manifest = json.loads((directory/'manifest.json').read_text())
        if manifest['run_id'] in identities:
            raise ValueError('duplicate campaign is not an independent trial')
        identities.add(manifest['run_id'])
        key = (manifest['source_sha256'], manifest['library_sha256'], manifest['sample_rate'],
               manifest['frames_per_user'], manifest['coding'], manifest['allocation_swap'],
               {m:w['active_rms'] for m,w in manifest['waveforms'].items()})
        if baseline is not None and key != baseline:
            raise ValueError('source/build or experiment settings differ; stratify these trials')
        baseline = key
        results = {mode:json.loads((directory/f'{mode}-result.json').read_text()) for mode in MODES}
        if any(r['run_id'] != manifest['run_id'] or r['mode'] != m for m,r in results.items()):
            raise ValueError('trial/result identity mismatch')
        rf_settings = []
        for result in results.values():
            if not result['tx']['hardware_opened'] or not result['tx']['complete']:
                raise ValueError('only completed hardware trials may enter an OTA summary')
            rf_settings.append((result['tx']['sdr'], {u:r['sdr'] for u,r in result['rx'].items()}))
        if any(settings != rf_settings[0] for settings in rf_settings):
            raise ValueError('RF settings differ between paired modes')
        if baseline_rf is not None and rf_settings[0] != baseline_rf:
            raise ValueError('RF settings differ across trial blocks; stratify these trials')
        baseline_rf = rf_settings[0]
        trials.append(results)
    rng = np.random.default_rng(2026)
    indices = rng.integers(0, len(trials), (10000, len(trials)))
    def interval(values):
        values = np.asarray(values, float)
        return dict(mean=float(np.mean(values)), bootstrap95=list(np.quantile(np.mean(values[indices], axis=1), [.025,.975])))
    modes = {m:dict(aggregate_goodput_bps=interval([t[m]['aggregate_goodput_bps'] for t in trials]),
                    pdr={u:interval([t[m]['users'][u]['pdr'] for t in trials]) for u in ('1','2')},
                    jain_fairness=interval([t[m]['jain_fairness'] for t in trials])) for m in MODES}
    differences = {u:interval([t['native-ofdm']['users'][u]['pdr']-t['legacy-ofdm']['users'][u]['pdr']
                              for t in trials]) for u in ('1','2')}
    # Difference works even if baseline goodput is zero; a ratio would be undefined.
    gain = interval([t['native-ofdma']['aggregate_goodput_bps']-t['native-ofdm']['aggregate_goodput_bps'] for t in trials])
    return dict(trials=len(trials), modes=modes, native_ofdm_minus_legacy_pdr=differences,
                ofdma_minus_native_ofdm_goodput_bps=gain, pdr_noninferiority_margin=margin,
                native_ofdm_meets_pdr_margin=all(v['bootstrap95'][0] >= -margin for v in differences.values()),
                ofdma_goodput_gain_supported=gain['bootstrap95'][0] > 0,
                caveat='trial-level bootstrap; use >=10 randomized paired blocks; RF power/geometry/calibration require operator review')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trials', type=Path, nargs='+')
    parser.add_argument('--margin', type=float, default=.01)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.margin < 1:
        parser.error('margin must be in [0,1)')
    result = summarize(args.trials, args.margin)
    save_json(args.output, result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
