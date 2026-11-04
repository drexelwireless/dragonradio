#!/usr/bin/env python3
"""Full-stack OTA campaign: paired native/liquid trials. DRY RUN BY DEFAULT.

One campaign file (grid/ota-campaign.shared-pps.example.json) describes the
nodes, RF settings, PHYs, traffic and strata. This script expands it into one
scripts/ota-fullstack.py plan per trial: BLOCKS blocks, each running every PHY
once in a seeded random order, all at the same RF settings (so native and
liquid are paired by block, plan section 7.7). It is run by
grid/ota-pipeline.sh on the grid controller, but works on its own:

  ota-campaign.py plan CAMPAIGN.json --run-dir RUN [--reservation ID]
      write RUN/plans/*.json and validate each with ota-fullstack.py (nothing runs)
  ota-campaign.py run CAMPAIGN.json --run-dir RUN --reservation ID --execute
      RF TRANSMISSION: run the trials in order through ota-fullstack.py --execute,
      then summarize them with scripts/summarize-fullstack.py

A trial that already has a summary is skipped, so a rerun resumes. A trial
whose controller step fails (a node never became ready, an ssh or radio
start-up failure) stops the campaign: that is an infrastructure problem, not
an RF result. Trials that run but fail their checks are kept and the
campaign continues.
"""
import argparse
import importlib.util
import json
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ota = load('ota_fullstack', 'scripts/ota-fullstack.py')

NODE_KEYS = ('id', 'host', 'family', 'addr', 'tx_gain', 'rx_gain', 'clock_source', 'time_source',
             'sync_time_to_pps', 'radio_args', 'host_key_alias')


class CampaignError(ValueError):
    pass


def ssh_options(campaign, run_dir, reservation):
    """ssh options for grid containers: through the login server, with host
    keys pinned per reservation (first contact is the pipeline's install
    stage, which accepts new keys into this file)."""
    s = campaign['ssh']
    key = str(Path(s['key']).expanduser())
    proxy = (f'ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i {key} -o LogLevel=ERROR '
             f'-o ConnectTimeout=60 -W %h:%p {s["head"]}')
    return ['-o', 'IdentitiesOnly=yes', '-i', key, '-o', 'GSSAPIAuthentication=no', '-o', 'LogLevel=ERROR',
            '-o', f'ProxyCommand={proxy}'], str(Path(run_dir) / f'known_hosts.{reservation}')


def trial_order(campaign):
    """[(block, phy)]: every PHY once per block, in a seeded random order."""
    rng = random.Random(campaign.get('seed', 1))
    order = []
    for b in range(1, campaign['blocks'] + 1):
        phys = sorted(campaign['phys'])
        rng.shuffle(phys)
        order += [(b, phy) for phy in phys]
    return order


def plans(campaign, run_dir, reservation):
    """The ota-fullstack.py plan for every trial, in run order."""
    for key in ('campaign_id', 'rf_approval_reference', 'frequency', 'bandwidth', 'max_tx_gain', 'nodes',
                'phys', 'blocks', 'workspace', 'workspace_commit', 'results_root', 'node_local_root',
                'traffic', 'strata', 'ssh'):
        if key not in campaign:
            raise CampaignError(f'campaign lacks {key}')
    if not 1 <= campaign['blocks'] <= 20:
        raise CampaignError('blocks must be 1..20')
    if not campaign['phys'] or not set(campaign['phys']) <= set(ota.PHYS):
        raise CampaignError(f'phys must be a subset of {sorted(ota.PHYS)}')
    if not 0 < campaign['max_tx_gain'] <= 20:
        raise CampaignError('max_tx_gain must be in (0, 20] dB (the grid OTA cap)')
    for n in campaign['nodes']:
        if n.get('tx_gain', 0) > campaign['max_tx_gain']:
            raise CampaignError(f'node {n.get("id")}: tx_gain {n["tx_gain"]} exceeds max_tx_gain {campaign["max_tx_gain"]}')
    opts, known_hosts = ssh_options(campaign, run_dir, reservation)
    out = []
    for block, phy in trial_order(campaign):
        plan = {
            'trial_id': f'{campaign["campaign_id"]}-b{block:02d}-{phy}',
            'reservation_id': str(reservation),
            'rf_approval_reference': campaign['rf_approval_reference'],
            'rf_approval_covers': campaign.get('rf_approval_covers', []),
            'results_root': campaign['results_root'],
            'node_local_root': campaign['node_local_root'],
            'workspace_commit': campaign['workspace_commit'],
            'phy': phy,
            'radio_args': campaign['phys'][phy],
            'frequency': campaign['frequency'],
            'bandwidth': campaign['bandwidth'],
            'timesync': campaign.get('timesync', {'time_master': 1}),
            'traffic': campaign['traffic'],
            'strata': dict(campaign['strata'], pair_block=block),
            'privilege': campaign.get('privilege', 'auto'),
            'netns': campaign.get('netns', False),
            'ssh_options': opts,
            'known_hosts': known_hosts,
            'nodes': [dict({k: n[k] for k in NODE_KEYS if k in n}, workspace=campaign['workspace'])
                      for n in campaign['nodes']],
        }
        plan.update(campaign.get('trial', {}))
        out.append(plan)
    return out


def write_plans(campaign, run_dir, reservation):
    pdir = Path(run_dir) / 'plans'
    pdir.mkdir(parents=True, exist_ok=True)
    result = []
    for plan in plans(campaign, run_dir, reservation):
        try:
            built = ota.build(plan)          # validates; executes nothing
        except ota.PlanError as e:
            raise CampaignError(f'{plan["trial_id"]}: {e}') from e
        path = pdir / f'{plan["trial_id"]}.json'
        path.write_text(json.dumps(plan, indent=1))
        result.append((plan, path, built['warnings']))
    return result


def run(campaign, run_dir, reservation, python):
    written = write_plans(campaign, run_dir, reservation)
    log = Path(run_dir) / 'campaign.log'
    summaries = []

    def note(msg):
        line = f'{time.strftime("%F %T")} {msg}'
        print(line, flush=True)
        with open(log, 'a') as f:
            f.write(line + '\n')

    for plan, path, warnings in written:
        trial_dir = Path(plan['results_root']) / plan['trial_id']
        summary = trial_dir / 'summary.json'
        if summary.exists():
            note(f'{plan["trial_id"]}: already done, skipping')
            summaries.append(str(summary))
            continue
        if trial_dir.exists():
            aside = trial_dir.with_name(f'{trial_dir.name}.incomplete-{int(time.time())}')
            shutil.move(str(trial_dir), str(aside))
            note(f'{plan["trial_id"]}: earlier incomplete attempt moved to {aside.name}')
        for w in warnings:
            note(f'{plan["trial_id"]}: warning: {w}')
        note(f'{plan["trial_id"]}: start ({plan["phy"]}, block {plan["strata"]["pair_block"]})')
        rc = subprocess.call([python, str(ROOT / 'scripts' / 'ota-fullstack.py'), '--plan', str(path), '--execute'])
        if not summary.exists():
            note(f'{plan["trial_id"]}: no summary (exit {rc}); stopping the campaign: inspect {trial_dir}')
            return 3
        s = json.loads(summary.read_text())
        note(f'{plan["trial_id"]}: passed={s.get("passed")} continuity_ok={s.get("continuity_ok")} (exit {rc})')
        summaries.append(str(summary))
    out = Path(run_dir) / 'campaign-summary.json'
    subprocess.call([python, str(ROOT / 'scripts' / 'summarize-fullstack.py'), *summaries,
                     '--output', str(out), '--min-trials', str(min(3, campaign['blocks']))])
    note(f'campaign done: {len(summaries)} trials; summary {out}')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('action', choices=['plan', 'run'])
    ap.add_argument('campaign', type=Path)
    ap.add_argument('--run-dir', type=Path, required=True)
    ap.add_argument('--reservation', default='PENDING', help='grid reservation ID (known at run time)')
    ap.add_argument('--python', default=sys.executable, help='Python for ota-fullstack.py (needs h5py)')
    ap.add_argument('--execute', action='store_true', help='RUN THE TRIALS: opens radios and transmits')
    args = ap.parse_args(argv)
    campaign = json.loads(args.campaign.read_text())
    try:
        if args.action == 'plan':
            for plan, path, warnings in write_plans(campaign, args.run_dir, args.reservation):
                print(f'{path}: {plan["phy"]} block {plan["strata"]["pair_block"]}'
                      + ''.join(f'\n  warning: {w}' for w in warnings))
            print('dry run: nothing was executed')
            return 0
        if not args.execute:
            print('run needs --execute (RF transmission); use "plan" for a dry run', file=sys.stderr)
            return 2
        if args.reservation == 'PENDING':
            print('run needs --reservation', file=sys.stderr)
            return 2
        return run(campaign, args.run_dir, args.reservation, args.python)
    except (CampaignError, ota.PlanError) as e:
        print(f'campaign rejected: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
