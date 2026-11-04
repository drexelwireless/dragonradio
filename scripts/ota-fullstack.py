#!/usr/bin/env python3
"""Full-stack over-the-air trial runner (P4 item 2.4). DRY RUN BY DEFAULT.

The OTA counterpart of scripts/sim-fullstack.py: one production radio per
reserved node, each on a real USRP (--addr) instead of SimUSRP, with a tap
interface and kernel traffic applications (scripts/sim-netapp.py) in a
per-node, per-trial network namespace. The controller:

1. validates the plan (reservation and RF approval references, approval of
   reverse-link and feedback transmission, distinct hosts, absolute paths,
   finite durations, gains and rates in range, workspace commit);
2. writes one node script per node and prints the exact ssh command for each
   (dry run: nothing else happens, no connection is made);
3. with --execute only: starts every node (radio first), waits for every
   node's readiness file (the radio is up and its warm-up running), then
   writes a GO file that starts the applications on all nodes, waits for
   the finite trial to end under timeouts, collects node-local results to
   the shared results directory, checks continuity (radio and device error
   counts, TX acknowledgement from the production send log) and scores the
   trial with the same evaluation as the simulation (sim-fullstack.py
   evaluate_tap), adding the plan's strata for scripts/summarize-fullstack.py.

Nothing here allocates or stops reservations, flashes radios or changes
host configuration outside the trial namespace. The node script needs
`sudo -n` for the namespace, the moved USRP interface and the radio's
capabilities; that, the RF approval and the reservation are the operator's.

Usage:
  ota-fullstack.py --plan PLAN.json                 # validate and print commands
  ota-fullstack.py --plan PLAN.json --execute       # RF TRANSMISSION
"""
import argparse
import json
import shlex
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLACEHOLDER = 'REPLACE'
FAMILIES = ('N210', 'X310', 'B210')
PHYS = {'native-ofdm': ['--phy', 'native-ofdm'], 'ofdm': ['--phy', 'ofdm']}
APP_KINDS = {'udp', 'tcp-bulk', 'tcp-rr'}
# Patterns counted in each radio log (UHD and MAC errors); any count > 0
# invalidates the trial for RF-only loss conclusions (plan section 7.9).
CONTINUITY = {
    'tx_late': 'TX error: packet had time that was late',
    'tx_underflow': 'TX error: an internal send buffer has emptied',
    'rx_error': 'RX error:',
    'missed_slot': 'MISSED SLOT DEADLINE',
}


class PlanError(ValueError):
    pass


def require(cond, msg):
    if not cond:
        raise PlanError(msg)


def validate(plan):
    """Check a plan; returns a list of warnings. Raises PlanError."""
    warnings = []
    for key in ('reservation_id', 'rf_approval_reference', 'trial_id', 'results_root', 'node_local_root',
                'workspace_commit', 'phy', 'frequency', 'bandwidth', 'duration_s', 'nodes', 'traffic'):
        require(key in plan, f'plan lacks {key}')
    for key in ('reservation_id', 'rf_approval_reference', 'trial_id'):
        require(isinstance(plan[key], str) and plan[key] and PLACEHOLDER not in plan[key], f'{key} is not filled in')
    covers = set(plan.get('rf_approval_covers', []))
    require({'forward data', 'reverse data and ACK', 'timesync HELLO broadcasts'} <= covers,
            'rf_approval_covers must include forward data, reverse data and ACK, and timesync HELLO broadcasts: '
            'TDMA with ARQ transmits from every node')
    for key in ('results_root', 'node_local_root'):
        require(plan[key].startswith('/'), f'{key} must be an absolute path')
    require(plan['phy'] in PHYS, f'phy must be one of {sorted(PHYS)}')
    require(isinstance(plan['frequency'], (int, float)) and 70e6 <= plan['frequency'] <= 6e9, 'frequency out of range')
    require(plan['bandwidth'] in (1e6, 2e6, 5e6), 'bandwidth must be 1, 2 or 5 MHz (rates the soak verified)')
    require(5 <= plan['duration_s'] <= 600, 'duration_s must be 5..600 (finite trials)')
    require(5 <= plan.get('warmup_s', 20) <= 120, 'warmup_s must be 5..120')
    require(1 <= plan.get('drain_s', 5) <= 60, 'drain_s must be 1..60')
    commit = plan['workspace_commit']
    require(len(commit) == 40 and all(c in '0123456789abcdef' for c in commit), 'workspace_commit must be a full SHA-1')
    nodes = plan['nodes']
    require(2 <= len(nodes) <= 8, 'two to eight nodes')
    ids = [n.get('id') for n in nodes]
    require(sorted(ids) == list(range(1, len(nodes) + 1)), 'node ids must be 1..N')
    require(len({n.get('host') for n in nodes}) == len(nodes), 'hosts must be distinct')
    require(len({n.get('addr') for n in nodes}) == len(nodes), 'device addresses must be distinct')
    for n in nodes:
        for key in ('host', 'workspace', 'addr', 'family', 'tx_gain', 'rx_gain'):
            require(key in n and n[key] not in ('', None) and PLACEHOLDER not in str(n[key]),
                    f'node {n.get("id")}: {key} is not filled in')
        require(n['workspace'].startswith('/'), f'node {n["id"]}: workspace must be absolute')
        require(n['family'] in FAMILIES, f'node {n["id"]}: family must be one of {FAMILIES}')
        require(0 <= n['tx_gain'] <= 31.5 if n['family'] == 'N210' else 0 <= n['tx_gain'] <= 70,
                f'node {n["id"]}: tx_gain out of range for {n["family"]}')
        require(0 <= n['rx_gain'] <= 70, f'node {n["id"]}: rx_gain out of range')
        # N210 time source is none/external/mimo, never 'internal' (plan section 6).
        if n['family'] == 'N210':
            require(n.get('time_source', '') in ('', 'none', 'external', 'mimo'),
                    f'node {n["id"]}: N210 time_source must be empty, none, external or mimo')
        if n.get('usrp_iface'):
            require('usrp_iface_addr' in n, f'node {n["id"]}: usrp_iface needs usrp_iface_addr (host side, CIDR)')
    ts = plan.get('timesync', {})
    require(ts.get('time_master') in ids, 'timesync.time_master must be a node id: independent clocks need it')
    if all(n.get('time_source') == 'external' and n.get('clock_source') == 'external' for n in nodes):
        warnings.append('all nodes on external PPS/10 MHz: timesync assumes no skew (radio/timesync.py)')
    for t in plan['traffic']:
        require(t.get('kind') in APP_KINDS, f'traffic kind must be one of {sorted(APP_KINDS)}')
        require(t.get('src') in ids and t.get('dst') in ids and t['src'] != t['dst'], 'traffic src/dst must be distinct node ids')
        require(1024 <= t.get('port', 0) < 65536, 'traffic port must be 1024..65535')
    strata = plan.get('strata', {})
    for key in ('pair_type', 'strength', 'placement', 'power', 'clock', 'carrier_map'):
        require(key in strata and strata[key] not in ('', None) and PLACEHOLDER not in str(strata[key]),
                f'strata.{key} is required (plan section 7.7: stratify, never average incompatible trials)')
    return warnings


def ip(node_id):
    return f'10.10.10.{node_id}'


def app_args(plan, node_id):
    """sim-netapp.py arguments for this node from the plan's traffic list."""
    args, servers = [], set()
    for t in plan['traffic']:
        if t['src'] == node_id:
            if t['kind'] == 'udp':
                args += ['--udp-send', f'{ip(t["dst"])}:{t["port"]}:{t["pps"]}:{t["size"]}']
            elif t['kind'] == 'tcp-bulk':
                args += ['--tcp-bulk', f'{ip(t["dst"])}:{t["port"]}:{t["bytes"]}']
            else:
                args += ['--tcp-rr', f'{ip(t["dst"])}:{t["port"]}:{t["count"]}:{t["size"]}']
        if t['dst'] == node_id:
            if t['kind'] == 'udp':
                args += ['--udp-recv', str(t['port'])]
            elif t['port'] not in servers:
                args += ['--tcp-server', str(t['port'])]
                servers.add(t['port'])
    return args


def node_script(plan, node):
    """Shell script run on a node (bash -s over ssh). Deterministic text."""
    q = shlex.quote
    nid, trial = node['id'], plan['trial_id']
    ws = node['workspace']
    local = f'{plan["node_local_root"]}/{trial}/node{nid}'
    shared = f'{plan["results_root"]}/{trial}'
    ns = f'drfs-{trial}'[:15]
    warmup, duration, drain = plan.get('warmup_s', 20), plan['duration_s'], plan.get('drain_s', 5)
    go_wait = plan.get('go_timeout_s', 60)
    total = int(warmup + duration + drain + go_wait + 30)
    ts = plan['timesync']
    radio = [f'{ws}/dragonradio/dragonradio', f'{ws}/dragonradio/scripts/sim-node.py', '--hardware',
             '--addr', node['addr'], '-i', str(nid), '-n', str(len(plan['nodes'])),
             '-f', str(plan['frequency']), '-b', str(plan['bandwidth']),
             '--tx-gain', str(node['tx_gain']), '--rx-gain', str(node['rx_gain'])]
    if node.get('clock_source'):
        radio += ['--clock-source', node['clock_source']]
    if node.get('time_source'):
        radio += ['--time-source', node['time_source']]
    radio += PHYS[plan['phy']] + plan.get('radio_args', []) + node.get('radio_args', [])
    radio += ['--arq', '-l', f'{local}/logs', '--log-invalid-headers',
              '--time-master', str(ts['time_master']), '--hello-period', str(ts.get('hello_period', 0.5)),
              '--timesync-period', str(ts.get('period', 2.0)),
              '--warmup', str(warmup + go_wait), '--duration', str(duration), '--drain', str(drain),
              '--ready-file', f'{local}/ready.json', '--results', f'{local}/node{nid}.json']
    app = [f'{ws}/venv/bin/python', f'{ws}/dragonradio/scripts/sim-netapp.py'] + app_args(plan, nid) + [
        '--duration', str(duration), '--linger', str(drain), '--results', f'{local}/app{nid}.json']
    patterns = ' '.join(f'{k}={q(v)}' for k, v in CONTINUITY.items())
    nsexec = f'sudo -n ip netns exec {ns}'
    lines = [
        '#!/bin/bash',
        f'# Full-stack OTA trial {trial}, node {nid} ({node["family"]} {node["addr"]} on {node["host"]}).',
        f'# Reservation {plan["reservation_id"]}; RF approval {plan["rf_approval_reference"]}. Generated by ota-fullstack.py.',
        'set -euo pipefail',
        f'LOCAL={q(local)}; SHARED={q(shared)}; NS={q(ns)}',
        'mkdir -p "$LOCAL/logs"',
        'cleanup() {',
        '  set +e',
        '  [ -n "${RADIO:-}" ] && kill -INT "$RADIO" 2>/dev/null && sleep 5 && kill -KILL "$RADIO" 2>/dev/null',
        '  [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null',
    ]
    if node.get('usrp_iface'):
        lines += [f'  {nsexec} ip link set {q(node["usrp_iface"])} netns 1 2>/dev/null',
                  f'  sudo -n ip addr replace {q(node["usrp_iface_addr"])} dev {q(node["usrp_iface"])} 2>/dev/null',
                  f'  sudo -n ip link set {q(node["usrp_iface"])} up 2>/dev/null']
    lines += ['  sudo -n ip netns del "$NS" 2>/dev/null',
              '  mkdir -p "$SHARED/node' + str(nid) + '" && cp -r "$LOCAL"/. "$SHARED/node' + str(nid) + '/"',
              '}',
              'trap cleanup EXIT INT TERM',
              '# Preflight (read-only): workspace commit, no other radio, disk space.',
              f'[ "$(cat {q(ws)}/dragonradio/COMMIT 2>/dev/null)" = {q(plan["workspace_commit"])} ] || {{ echo "workspace commit mismatch"; exit 10; }}',
              '! pgrep -x dragonradio >/dev/null || { echo "another dragonradio is running"; exit 11; }',
              '[ "$(df --output=avail -k "$LOCAL" | tail -1)" -gt 2000000 ] || { echo "less than 2 GB free on node-local disk"; exit 12; }',
              f'# Private network namespace for the tap interface and applications.',
              'sudo -n ip netns add "$NS"',
              f'{nsexec} ip link set lo up']
    if node.get('usrp_iface'):
        lines += [f'# The USRP Ethernet interface moves into the namespace for the trial (restored on exit).',
                  f'sudo -n ip link set {q(node["usrp_iface"])} netns "$NS"',
                  f'{nsexec} ip addr add {q(node["usrp_iface_addr"])} dev {q(node["usrp_iface"])}',
                  f'{nsexec} ip link set {q(node["usrp_iface"])} up']
    lines += [f'# Radio (finite: timeout {total} s).',
              f'{nsexec} timeout --signal=INT --kill-after=10s {total}s {shlex.join(radio)} > "$LOCAL/radio.log" 2>&1 &',
              'RADIO=$!',
              f'# Readiness barrier: radio up, then wait for the controller\'s GO (all nodes ready).',
              f'for i in $(seq 1 {int(warmup * 10)}); do [ -e "$LOCAL/ready.json" ] && break; kill -0 "$RADIO" || exit 13; sleep 0.1; done',
              '[ -e "$LOCAL/ready.json" ] || { echo "radio did not become ready"; exit 14; }',
              f'cp "$LOCAL/ready.json" "$SHARED/node{nid}-ready.json"',
              f'for i in $(seq 1 {int(go_wait * 10)}); do [ -e "$SHARED/GO" ] && break; sleep 0.1; done',
              '[ -e "$SHARED/GO" ] || { echo "no GO from controller"; exit 15; }',
              '# Applications start 2 s after GO on this host\'s CLOCK_MONOTONIC (servers listen at once).',
              'START=$(python3 -c "import time; print(time.monotonic() + 2.0)")',
              f'{nsexec} timeout --signal=INT --kill-after=5s {int(duration + drain + 15)}s {shlex.join(app)} --start-at "$START" > "$LOCAL/app.log" 2>&1 &',
              'APP=$!',
              'wait "$APP" || echo "app exit $?"',
              'APP=',
              'wait "$RADIO" || echo "radio exit $?"',
              'RADIO=',
              '# Continuity: radio and device errors from the radio log.',
              f'python3 - "$LOCAL/radio.log" "$LOCAL/continuity.json" {patterns} <<\'PY\'',
              'import json, sys',
              'log = open(sys.argv[1], errors="replace").read()',
              'counts = {k: log.count(v) for k, v in (a.split("=", 1) for a in sys.argv[3:])}',
              'json.dump(counts, open(sys.argv[2], "w"))',
              'print("continuity", counts)',
              'PY',
              '']
    return '\n'.join(lines)


def ssh_command(plan, node):
    ssh = ['ssh', '-x', '-F', '/dev/null', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
           '-o', 'StrictHostKeyChecking=yes']
    if node.get('host_key_alias'):
        ssh += ['-o', 'HostKeyAlias=' + node['host_key_alias']]
    if plan.get('known_hosts'):
        ssh += ['-o', 'UserKnownHostsFile=' + plan['known_hosts']]
    return ssh + [node['host'], 'bash -s']


def build(plan):
    warnings = validate(plan)
    trial_dir = f'{plan["results_root"]}/{plan["trial_id"]}'
    nodes = {}
    for node in plan['nodes']:
        nodes[str(node['id'])] = {'host': node['host'], 'ssh': ssh_command(plan, node),
                                  'stdin_script': f'{trial_dir}/node{node["id"]}.sh', 'script': node_script(plan, node)}
    return {'trial_id': plan['trial_id'], 'trial_dir': trial_dir, 'warnings': warnings, 'nodes': nodes,
            'go_file': f'{trial_dir}/GO', 'hardware_opened': False}


def score(plan, trial_dir):
    """Score a completed trial with the simulation's evaluation (tap scenarios)."""
    sys.path.insert(0, str(ROOT / 'scripts'))
    import importlib.util
    spec = importlib.util.spec_from_file_location('simfs', ROOT / 'scripts' / 'sim-fullstack.py')
    simfs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(simfs)
    ids = [n['id'] for n in plan['nodes']]
    results, apps, continuity = {}, {}, {}
    for n in ids:
        d = Path(trial_dir) / f'node{n}'
        results[n] = json.loads((d / f'node{n}.json').read_text())
        apps[n] = json.loads((d / f'app{n}.json').read_text())
        continuity[n] = json.loads((d / 'continuity.json').read_text())
        dev = results[n]['device']
        dev['rx_late'] = continuity[n].get('rx_error', 0)
        dev['rx_overflows'] = continuity[n].get('rx_error', 0)
    name = f'ota-{plan["trial_id"]}'
    simfs.SCENARIOS[name] = dict(doc='OTA trial', arq=True, links={}, tap=True, flows={},
                                 apps={n: app_args(plan, n) for n in ids},
                                 expect=dict(min_udp_delivery=0.0, max_app_duplicates=0))
    # The logs live under trial_dir/nodeN/logs; evaluate_tap reads OUTDIR/logs/node-NNN.
    logs = Path(trial_dir) / 'logs'
    logs.mkdir(exist_ok=True)
    for n in ids:
        target = logs / f'node-{n:03d}'
        if not target.exists():
            target.symlink_to(Path(trial_dir) / f'node{n}' / 'logs' / f'node-{n:03d}')
    summary = simfs.evaluate_tap(name, Path(trial_dir), results, apps, {}, ids)
    summary['continuity'] = {str(k): v for k, v in continuity.items()}
    summary['continuity_ok'] = all(c == 0 for v in continuity.values() for c in v.values())
    summary['strata'] = dict(plan['strata'], phy=plan['phy'], bandwidth=plan['bandwidth'],
                             frequency=plan['frequency'], build=plan['workspace_commit'])
    summary['plan'] = {k: plan[k] for k in ('reservation_id', 'rf_approval_reference', 'trial_id')}
    summary['valid_for_rf_conclusions'] = summary['continuity_ok']
    return summary


def execute(plan, built):
    trial_dir = Path(built['trial_dir'])
    trial_dir.mkdir(parents=True, exist_ok=False)   # a fresh directory per trial
    (trial_dir / 'plan.json').write_text(json.dumps(plan, indent=1))
    procs, logs = {}, []
    try:
        for nid, n in built['nodes'].items():
            path = trial_dir / f'node{nid}.sh'
            path.write_text(n['script'])
            log = open(trial_dir / f'node{nid}.ssh.log', 'x')
            logs.append(log)
            procs[nid] = subprocess.Popen(n['ssh'], stdin=open(path), stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + plan.get('warmup_s', 20) + 30
        while not all((trial_dir / f'node{nid}-ready.json').exists() for nid in procs):
            if time.monotonic() > deadline or any(p.poll() is not None for p in procs.values()):
                raise RuntimeError('not every node became ready; no GO written; inspect node logs')
            time.sleep(0.2)
        (trial_dir / 'GO').write_text(json.dumps({'wall': time.time()}))
        limit = plan.get('warmup_s', 20) + plan.get('go_timeout_s', 60) + plan['duration_s'] + plan.get('drain_s', 5) + 90
        for nid, p in procs.items():
            p.wait(timeout=limit)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.terminate()
        for log in logs:
            log.close()
    summary = score(plan, trial_dir)
    (trial_dir / 'summary.json').write_text(json.dumps(summary, indent=1))
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--plan', type=Path, required=True)
    ap.add_argument('--execute', action='store_true', help='RUN THE TRIAL: opens radios and transmits')
    ap.add_argument('--write-scripts', type=Path, help='dry run: also write the node scripts to this directory')
    args = ap.parse_args(argv)
    plan = json.loads(args.plan.read_text())
    try:
        built = build(plan)
    except PlanError as e:
        print(f'plan rejected: {e}', file=sys.stderr)
        return 2
    if not args.execute:
        if args.write_scripts:
            args.write_scripts.mkdir(parents=True, exist_ok=True)
            for nid, n in built['nodes'].items():
                (args.write_scripts / f'node{nid}.sh').write_text(n['script'])
        print(json.dumps(dict(built, dry_run=True,
                              note='nothing was executed; rerun with --execute in the approved window'), indent=1))
        return 0
    summary = execute(plan, built)
    print(json.dumps({k: summary[k] for k in ('passed', 'continuity_ok', 'valid_for_rf_conclusions')}, indent=1))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
