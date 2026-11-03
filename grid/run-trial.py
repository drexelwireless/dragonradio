#!/usr/bin/env python3
"""Controller-side execution on three ALREADY reserved and configured nodes.

No gridcli, reservation mutation, firmware changes, or host configuration. The
explicit --execute action includes RF TX and needs the lab's approved plan.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--mode', choices=('legacy-ofdm', 'native-ofdm', 'native-ofdma'), required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text())
    if (not plan.get('reservation_id') or not plan.get('rf_approval_reference') or
            len({plan[r]['host'] for r in ('tx', 'rx1', 'rx2')}) != 3):
        raise ValueError('record reservation, RF approval reference and three distinct hosts')
    shared = Path(plan['shared_results'])
    if not shared.is_absolute() or not Path(plan['campaign']).is_absolute():
        raise ValueError('shared_results and campaign must be absolute paths visible on all nodes')
    duration = float(plan.get('capture_seconds', 15))
    if not 5 <= duration <= 30:
        raise ValueError('capture_seconds must be 5..30')
    ready = [shared/f'{args.mode}-u{u}-ready.json' for u in (1, 2)]
    commands = {}
    for role in ('rx1', 'rx2', 'tx'):
        node = plan[role]
        if any(not node[k] for k in ('host', 'root', 'python', 'sdr_config')):
            raise ValueError('complete node settings required')
        command = [node['python'], str(Path(node['root'])/'scripts/ota-phy.py'),
                   'tx' if role == 'tx' else 'rx', '--campaign', plan['campaign'],
                   '--mode', args.mode, '--sdr-config', node['sdr_config'], '--execute']
        if role == 'tx':
            command += ['--require-ready', *map(str, ready), '--output', str(shared/f'{args.mode}-tx.json')]
        else:
            user = int(role[-1])
            command += ['--duration', str(duration), '--user', str(user), '--ready', str(ready[user-1]),
                        '--output', str(shared/f'{args.mode}-u{user}.cf32')]
        # Strict verified host keys. Optional reservation-scoped aliases use the
        # operator's already populated known-hosts file. Never accept changed keys.
        ssh = ['ssh', '-x', '-F', '/dev/null', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
               '-o', 'StrictHostKeyChecking=yes']
        if node.get('host_key_alias'):
            ssh += ['-o', 'HostKeyAlias='+node['host_key_alias']]
        if plan.get('known_hosts'):
            ssh += ['-o', 'UserKnownHostsFile='+plan['known_hosts']]
        ssh += [node['host'], shlex.join(['timeout', '--signal=INT', '--kill-after=5s',
                                         str(int(duration+10))+'s', *command])]
        commands[role] = ssh
    if not args.execute:
        print(json.dumps(dict(commands=commands, hardware_opened=False), indent=2))
        return
    shared.mkdir(parents=True, exist_ok=True)
    if any(p.exists() for p in ready) or any(shared.glob(args.mode+'-*.json')) or any(shared.glob(args.mode+'-*.cf32')):
        raise ValueError('use fresh shared_results for each trial; evidence/readiness already exists')
    processes, logs = {}, []
    status = dict(mode=args.mode, reservation_id=plan['reservation_id'],
                  rf_approval_reference=plan['rf_approval_reference'], complete=False)
    try:
        # Sequential dispatch avoids opening many SSH sessions against the head.
        for role in ('rx1', 'rx2'):
            log = (shared/f'{args.mode}-{role}.log').open('x')
            logs.append(log)
            processes[role] = subprocess.Popen(commands[role], stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+min(10, duration-3)
        while not all(p.exists() for p in ready):
            if time.monotonic() > deadline or any(p.poll() is not None for p in processes.values()):
                raise RuntimeError('receivers did not arm; no TX started; inspect logs')
            time.sleep(.1)
        log = (shared/f'{args.mode}-tx.log').open('x')
        logs.append(log)
        processes['tx'] = subprocess.Popen(commands['tx'], stdout=log, stderr=subprocess.STDOUT)
        if processes['tx'].wait(timeout=duration+15) != 0:
            raise RuntimeError('TX failed; inspect bounded capture and TX logs')
        for role in ('rx1', 'rx2'):
            if processes[role].wait(timeout=duration+15) != 0:
                raise RuntimeError(role+' failed; no valid comparison')
        status['complete'] = True
    finally:
        # Remote timeout bounds every process even if the controller loses SSH.
        # Keep sessions until that deadline on failure to allow normal RX stop.
        for process in processes.values():
            if process.poll() is None:
                try:
                    process.wait(timeout=duration+15)
                except subprocess.TimeoutExpired:
                    process.terminate()
        for log in logs:
            log.close()
        status['exit_codes'] = {r:p.poll() for r,p in processes.items()}
        (shared/f'{args.mode}-controller.json').write_text(json.dumps(status, indent=2)+'\n')
    print(json.dumps(status, indent=2))


if __name__ == '__main__':
    main()
