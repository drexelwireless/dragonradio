#!/usr/bin/env python3
"""Full-stack simulation scenarios (phase P2).

Each scenario starts scripts/sim-air.py and one radio process per node
(dragonradio scripts/sim-node.py). Every node runs the production Radio:
native OFDM PHY, channelizer, synthesizer, SmartController (ARQ) or
DummyController, network queue and TDMA MAC, over a SimUSRP device. Synthetic
UDP traffic replaces the tap interface; the tap-* scenarios instead run each
node in an unprivileged network namespace (unshare -rn) with a real tap device
and kernel UDP/TCP traffic from scripts/sim-netapp.py. Runs are wall-clock paced: they use
real time on the host's monotonic clock, so CPU load matters and results are
not bit-reproducible. Nothing is injected below the network interface.

Delivery is counted from application sequence numbers. Transmission attempts
(including retransmissions and queue drops) come from each sender's production
HDF5 'send' log; every frame the receiver's PHY handed up, including invalid
payloads and headers, comes from an RxRecorder between channelizer and
controller.
"""
import argparse
import json
import os
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Scenarios. Links are directed: 'links' maps receiver -> [(src, gain_db,
# cfo_hz, delay_samples, erasure)]. Flows map sender -> [(dest, port, pps, bytes)].
SCENARIOS = {
    'clean-arq': dict(
        doc='Bidirectional UDP, clean links, ARQ on',
        arq=True,
        flows={1: [(2, 5001, 40, 500)], 2: [(1, 5002, 40, 500)]},
        links={},
        expect=dict(min_delivery=0.995, max_app_duplicates=0, min_frame_delivery=0.999)),
    'clean-noarq': dict(
        doc='Bidirectional UDP, clean links, ARQ off (DummyController)',
        arq=False,
        flows={1: [(2, 5001, 40, 500)], 2: [(1, 5002, 40, 500)]},
        links={},
        expect=dict(min_delivery=0.995, min_frame_delivery=0.999)),
    'erasure-noarq': dict(
        doc='20% burst erasure on 1->2, ARQ off: losses reach the application',
        arq=False,
        flows={1: [(2, 5001, 40, 500)]},
        links={2: [(1, 0.0, 0.0, 0, 0.2)]},
        expect=dict(max_delivery=0.95, min_delivery=0.5)),
    'erasure-arq': dict(
        doc='20% burst erasure on 1->2, ARQ on: retransmissions recover losses',
        arq=True,
        flows={1: [(2, 5001, 40, 500)]},
        links={2: [(1, 0.0, 0.0, 0, 0.2)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0, min_retransmissions=1)),
    'ack-loss': dict(
        doc='30% burst erasure on the reverse link 2->1 only (ACK loss), ARQ on',
        arq=True,
        flows={1: [(2, 5001, 40, 500)]},
        links={1: [(2, 0.0, 0.0, 0, 0.3)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0)),
    'asymmetric': dict(
        doc='Bidirectional; 1->2 at 10% erasure, 2->1 at 30%, different rates',
        arq=True,
        flows={1: [(2, 5001, 50, 400)], 2: [(1, 5002, 20, 1000)]},
        links={2: [(1, 0.0, 0.0, 0, 0.1)], 1: [(2, 0.0, 0.0, 0, 0.3)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0)),
    'saturation': dict(
        doc='1->2 offered 1.2 Mb/s (above TDMA capacity), ARQ on: full-stack goodput',
        arq=True,
        flows={1: [(2, 5001, 150, 1000)]},
        links={},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999),
        capacity=True),
    'impaired': dict(
        doc='Both links: -20 dB gain (about 25 dB SNR), 2 kHz CFO, 37-sample delay',
        arq=True,
        flows={1: [(2, 5001, 40, 500)], 2: [(1, 5002, 40, 500)]},
        links={2: [(1, -20.0, 2000.0, 37, 0.0)], 1: [(2, -20.0, -1500.0, 11, 0.0)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0)),
}

# Scenarios over real tap interfaces: each node runs in its own network
# namespace (unshare -rn) with its radio's tap0 at 10.10.10.<node> and
# scripts/sim-netapp.py using the kernel UDP/TCP stacks. 'apps' maps node ->
# sim-netapp.py arguments.
IP = '10.10.10.%d'
TAP_SCENARIOS = {
    'tap-udp': dict(
        doc='Kernel UDP over tap, both directions, 40 pkt/s x 500 B, ARQ on',
        arq=True, links={},
        apps={1: ['--udp-send', f'{IP % 2}:5001:40:500', '--udp-recv', '5002'],
              2: ['--udp-send', f'{IP % 1}:5002:40:500', '--udp-recv', '5001']},
        expect=dict(min_udp_delivery=0.995, max_app_duplicates=0, min_frame_delivery=0.999)),
    'tap-tcp-bulk': dict(
        doc='TCP bulk 400 kB 1->2 over tap, ARQ on',
        arq=True, links={},
        apps={1: ['--tcp-bulk', f'{IP % 2}:6001:400000'], 2: ['--tcp-server', '6001']},
        expect=dict(bulk_complete=True, min_frame_delivery=0.999),
        bulk_bound=True),
    'tap-tcp-rr': dict(
        doc='TCP request/response, 64 B echoes 1->2, ARQ on',
        arq=True, links={},
        apps={1: ['--tcp-rr', f'{IP % 2}:6001:150:64'], 2: ['--tcp-server', '6001']},
        expect=dict(rr_complete=True, min_frame_delivery=0.999)),
    'tap-bidirectional': dict(
        doc='Simultaneous TCP bulk both ways, TCP request/response and UDP both ways, ARQ on',
        arq=True, links={},
        apps={1: ['--tcp-server', '6001', '--tcp-bulk', f'{IP % 2}:6002:150000',
                  '--tcp-rr', f'{IP % 2}:6002:60:64',
                  '--udp-send', f'{IP % 2}:5001:10:300', '--udp-recv', '5002'],
              2: ['--tcp-server', '6002', '--tcp-bulk', f'{IP % 1}:6001:150000',
                  '--udp-send', f'{IP % 1}:5002:10:300', '--udp-recv', '5001']},
        expect=dict(bulk_complete=True, rr_complete=True, min_udp_delivery=0.995,
                    max_app_duplicates=0, min_frame_delivery=0.999)),
    'tap-tcp-erasure-arq': dict(
        doc='TCP bulk 200 kB 1->2 with 10% burst erasure both ways, ARQ on',
        arq=True,
        links={2: [(1, 0.0, 0.0, 0, 0.1)], 1: [(2, 0.0, 0.0, 0, 0.1)]},
        apps={1: ['--tcp-bulk', f'{IP % 2}:6001:200000'], 2: ['--tcp-server', '6001']},
        expect=dict(bulk_complete=True)),
    'tap-tcp-erasure-noarq': dict(
        doc='Same erasure, ARQ off: TCP alone recovers the losses',
        arq=False,
        links={2: [(1, 0.0, 0.0, 0, 0.1)], 1: [(2, 0.0, 0.0, 0, 0.1)]},
        apps={1: ['--tcp-bulk', f'{IP % 2}:6001:200000'], 2: ['--tcp-server', '6001']},
        expect=dict(bulk_complete=True)),
}
for _name, _sc in TAP_SCENARIOS.items():
    _sc['tap'] = True
    _sc.setdefault('flows', {})
SCENARIOS.update(TAP_SCENARIOS)


def node_command(args, scenario, node, sock, outdir):
    sc = SCENARIOS[scenario]
    cmd = [args.dragonradio, str(ROOT / 'scripts' / 'sim-node.py'),
           '-i', str(node), '-n', str(args.nodes),
           '--sim-air', sock]
    if not sc.get('tap'):
        cmd.append('--sim-traffic')
    cmd += [
           '--sim-noise-db', str(args.noise_db),
           '--phy', 'native-ofdm', '-b', str(args.bandwidth),
           '-c', 'none', '-k', 'none', '-m', 'qpsk',
           '-l', str(outdir / 'logs'), '--log-invalid-headers',
           '--warmup', str(args.warmup + (TAP_STARTUP if sc.get('tap') else 0)),
           '--duration', str(args.duration),
           '--drain', str(args.drain + (2*args.drain + 2 if sc.get('tap') else 0)),
           '--results', str(outdir / f'node{node}.json')]
    if sc['arq']:
        cmd.append('--arq')
    for (dest, port, pps, size) in sc['flows'].get(node, []):
        cmd += ['--flow', f'{dest}:{port}:{pps}:{size}']
    for (src, gain, cfo, delay, erasure) in sc['links'].get(node, []):
        cmd += ['--link', f'{src}:{gain}:{cfo}:{delay}:{erasure}']
    return cmd + args.extra


# Seconds allowed for a tap node's radio to start before applications begin.
TAP_STARTUP = 4.0


def tap_node_script(args, scenario, node, sock, outdir, start_at):
    """Shell script run as 'unshare -rn --kill-child sh SCRIPT' for one node."""
    sc = SCENARIOS[scenario]
    radio = node_command(args, scenario, node, sock, outdir)
    app = [sys.executable, str(ROOT / 'scripts' / 'sim-netapp.py')] + sc['apps'].get(node, []) + [
        '--start-at', f'{start_at:.6f}', '--duration', str(args.duration),
        '--linger', str(args.drain), '--results', str(outdir / f'app{node}.json')]
    q = lambda argv: ' '.join(shlex.quote(a) for a in argv)
    return '\n'.join([
        '# Private user and network namespace for one node (no host network change).',
        'set -u',
        'ip link set lo up',
        f'{q(radio)} &',
        'radio=$!',
        f'{q(app)}',
        'echo "app exit $?"',
        'wait $radio',
        'echo "radio exit $?"',
        '']) + '\n'


def read_send_log(logdir, node):
    """Rows of one node's production 'send' log (one per transmission attempt)."""
    try:
        import h5py
    except ImportError:
        return None
    path = Path(logdir) / f'node-{node:03d}' / 'radio.h5'
    if not path.exists():
        return None
    with h5py.File(path, 'r') as f:
        send = f['send'][:]
        # mono_timestamp is relative to the logger's monotonic origin, which
        # the radio stores (whole seconds) as the 'start' attribute.
        origin = float(f.attrs['start'])
    # Datasets are preallocated; unused rows are zero.
    return {'rows': send[send['timestamp'] != 0], 'origin': origin}


# Frames sent less than this long before the receiver's snapshot may still be
# in flight (slot timing, RX latency, demodulation) and are not counted.
IN_FLIGHT = 0.25


def link_accounting(send, frames, src, dst, snapshot):
    """Per-attempt accounting for frames from src to dst.

    send: src's production send log; frames: dst's RxRecorder records taken at
    host monotonic time snapshot.
    """
    import numpy as np
    out = {}
    if send is not None:
        rows = send['rows']
        rows = rows[rows['mono_timestamp'] + send['origin'] <= snapshot - IN_FLIGHT]
        to_dst = rows[(rows['nexthop'] == dst) | (rows['nexthop'] == 255)]
        data = to_dst[to_dst['data_len'] > 0]
        out['frames_sent'] = int(len(to_dst))
        out['data_attempts'] = int(len(data))
        out['data_first_attempts'] = int(np.sum(data['nretrans'] == 0))
        out['retransmissions'] = int(np.sum(data['nretrans'] > 0))
        out['queue_or_deadline_drops'] = int(np.sum(to_dst['dropped'] != 0))
    from_src = [r for r in frames if r[1] == src and r[0] <= snapshot - IN_FLIGHT]
    out['frames_received'] = len(from_src)
    out['data_received_valid'] = sum(1 for r in from_src if r[6] and r[7] > 0)
    out['payload_invalid'] = sum(1 for r in from_src if r[5] and not r[6])
    if 'frames_sent' in out and out['frames_sent']:
        out['frame_delivery'] = sum(1 for r in from_src if r[6])/out['frames_sent']
    if out.get('data_attempts'):
        out['data_attempt_delivery'] = out['data_received_valid']/out['data_attempts']
    return out


def evaluate(scenario, outdir, results, air, nodes):
    sc = SCENARIOS[scenario]
    flows = []
    for src, fl in sc['flows'].items():
        for (dest, port, pps, size) in fl:
            sent = next((f['sent'] for f in results[src]['flows'] if f['port'] == port), 0)
            rec = results[dest]['received'].get(f'{src}:{port}', {})
            unique = rec.get('unique', 0)
            seqs = set(rec.get('seqs', []))
            # Diagnostic: duplicates of app seq 0 seen with repeated SYNs. Before
            # the receive-window fix, every retransmitted SYN redelivered the
            # first packet; any duplicate now fails the run.
            syns = sum(1 for r in results[dest].get('rx_frames', []) if r[1] == src and r[4] & 1 and r[6])
            dups = rec.get('dup_seqs', [])
            syn_redeliveries = min(dups.count(0), max(0, syns - 1))
            flows.append({
                'flow': f'{src}->{dest}:{port}', 'offered_pps': pps, 'size': size,
                'sent': sent, 'delivered': unique,
                'delivery': unique/sent if sent else None,
                'missing': len(set(range(sent)) - seqs),
                'app_duplicates': rec.get('duplicates', 0),
                'app_duplicate_seqs': dups,
                'syn_frames_received': syns,
                'syn_redeliveries': syn_redeliveries,
                'app_duplicates_other': rec.get('duplicates', 0) - syn_redeliveries,
                'corrupt': rec.get('corrupt', 0),
                'out_of_order': rec.get('out_of_order', 0),
                # Over the arrival window: a saturated flow keeps draining its
                # queue after the traffic generator stops.
                'goodput_kbps': (8*rec['bytes']/(rec['last_rx'] - rec['first_rx'])/1e3
                                 if rec.get('unique', 0) > 1 and rec['last_rx'] > rec['first_rx'] else 0.0),
                'latency_p50_ms': None if rec.get('latency_p50') is None else 1e3*rec['latency_p50'],
                'latency_p95_ms': None if rec.get('latency_p95') is None else 1e3*rec['latency_p95'],
            })
    sends = {n: read_send_log(outdir / 'logs', n) for n in nodes}
    links = {}
    for src in nodes:
        for dst in nodes:
            if src != dst:
                links[f'{src}->{dst}'] = link_accounting(sends[src], results[dst].get('rx_frames', []), src, dst,
                                                         results[dst]['snapshot_mono'])
    invalid_headers = {n: sum(1 for r in results[n].get('rx_frames', []) if not r[5]) for n in nodes}
    device = {n: results[n]['device'] for n in nodes}
    capacity = None
    if sc.get('capacity'):
        capacity = {}
        for src, fl in sc['flows'].items():
            send = sends[src]['rows'] if sends[src] is not None else None
            for (dest, port, pps, size) in fl:
                data = send[(send['nexthop'] == dest) & (send['data_len'] > 0)] if send is not None else []
                if not len(data):
                    continue
                ns = float(sum(int(x) for x in data['nsamples']))/len(data)
                fs = results[src]['tx_rate']
                slot, guard, nslots = 0.035, 0.010, len(nodes)
                # SlotSynthesizer::push accepts a frame only if it ends within
                # the slot minus guard (superslots off), so whole frames per slot:
                per_slot = int((slot - guard)*fs // ns)
                bound = per_slot*size*8/(slot*nslots)
                fl_out = next(f for f in flows if f['flow'] == f'{src}->{dest}:{port}')
                capacity[f'{src}->{dest}'] = {
                    'samples_per_frame': ns, 'frames_per_slot': per_slot,
                    'tdma_bound_kbps': bound/1e3, 'goodput_kbps': fl_out['goodput_kbps'],
                    'efficiency': fl_out['goodput_kbps']*1e3/bound if bound else None}
    exp = sc['expect']
    checks = []
    for f in flows:
        if 'min_delivery' in exp:
            checks.append((f'{f["flow"]} delivery >= {exp["min_delivery"]}', f['delivery'] is not None and f['delivery'] >= exp['min_delivery']))
        if 'max_delivery' in exp:
            checks.append((f'{f["flow"]} delivery <= {exp["max_delivery"]}', f['delivery'] is not None and f['delivery'] <= exp['max_delivery']))
        if 'max_app_duplicates' in exp:
            checks.append((f'{f["flow"]} application duplicates == 0', f['app_duplicates'] <= exp['max_app_duplicates']))
        checks.append((f'{f["flow"]} no corrupt payloads', f['corrupt'] == 0))
    if 'min_retransmissions' in exp:
        retx = sum(l.get('retransmissions', 0) for l in links.values())
        avail = all(s is not None for s in sends.values())
        checks.append((f'retransmissions >= {exp["min_retransmissions"]} (logged {retx})', avail and retx >= exp['min_retransmissions']))
    if 'min_frame_delivery' in exp:
        for name, l in links.items():
            if l.get('frames_sent'):
                checks.append((f'{name} PHY frame delivery >= {exp["min_frame_delivery"]}', l.get('frame_delivery', 0) >= exp['min_frame_delivery']))
    for n in nodes:
        d = device[n]
        checks.append((f'node {n}: no late TX bursts', d['tx_late'] == 0))
        checks.append((f'node {n}: no late or dropped RX bursts', d['rx_late'] == 0 and d['rx_queue_drops'] == 0 and d['rx_overflows'] == 0))
    return {
        'scenario': scenario, 'doc': sc['doc'], 'arq': sc['arq'],
        'links': {str(k): v for k, v in sc['links'].items()},
        'flows': flows, 'device': {str(k): v for k, v in device.items()},
        'links_accounting': links, 'invalid_headers': {str(k): v for k, v in invalid_headers.items()}, 'air': air,
        'capacity': capacity,
        'checks': [{'check': c, 'passed': bool(ok)} for c, ok in checks],
        'passed': all(ok for _, ok in checks),
    }


def common_checks(nodes, results, sends, exp, checks):
    """Per-link attempt accounting and device checks shared by all scenarios."""
    links = {}
    for src in nodes:
        for dst in nodes:
            if src != dst:
                links[f'{src}->{dst}'] = link_accounting(sends[src], results[dst].get('rx_frames', []), src, dst,
                                                         results[dst]['snapshot_mono'])
    if 'min_frame_delivery' in exp:
        for name, l in links.items():
            if l.get('frames_sent'):
                checks.append((f'{name} PHY frame delivery >= {exp["min_frame_delivery"]}', l.get('frame_delivery', 0) >= exp['min_frame_delivery']))
    device = {n: results[n]['device'] for n in nodes}
    for n in nodes:
        d = device[n]
        checks.append((f'node {n}: no late TX bursts', d['tx_late'] == 0))
        checks.append((f'node {n}: no late or dropped RX bursts', d['rx_late'] == 0 and d['rx_queue_drops'] == 0 and d['rx_overflows'] == 0))
    invalid_headers = {n: sum(1 for r in results[n].get('rx_frames', []) if not r[5]) for n in nodes}
    return links, device, invalid_headers


def evaluate_tap(scenario, outdir, results, apps, air, nodes):
    """Score a tap scenario from sim-netapp.py results and radio logs."""
    sc = SCENARIOS[scenario]
    exp = sc['expect']
    checks = []
    for n in nodes:
        checks.append((f'node {n}: application results present', n in apps))
        if n in apps:
            checks.append((f'node {n}: no application errors {apps[n]["errors"]}', not apps[n]['errors']))
            checks.append((f'node {n}: all clients finished', apps[n]['unfinished_clients'] == 0))
    flows, bulk, rr = [], [], []
    for n, app in apps.items():
        for f in app['udp_send']:
            dst = int(f['dest'].rsplit('.', 1)[1])
            rec = apps.get(dst, {}).get('udp_recv', {}).get(str(f['port']), {})
            unique = rec.get('unique', 0)
            flows.append({
                'flow': f'{n}->{dst}:{f["port"]}', 'kind': 'udp', 'offered_pps': f['pps'], 'size': f['size'],
                'sent': f['sent'], 'delivered': unique, 'delivery': unique/f['sent'] if f['sent'] else None,
                'app_duplicates': rec.get('duplicates', 0), 'corrupt': rec.get('corrupt', 0),
                'out_of_order': rec.get('out_of_order', 0),
                'latency_p50_ms': None if rec.get('latency_p50') is None else 1e3*rec['latency_p50'],
                'latency_p95_ms': None if rec.get('latency_p95') is None else 1e3*rec['latency_p95']})
        for b in app['tcp_bulk']:
            bulk.append(dict(b, src=n, dst=int(b['dest'].rsplit('.', 1)[1])))
        for r in app['tcp_rr']:
            rr.append(dict(r, src=n, dst=int(r['dest'].rsplit('.', 1)[1])))
    for f in flows:
        if 'min_udp_delivery' in exp:
            checks.append((f'{f["flow"]} UDP delivery >= {exp["min_udp_delivery"]}', f['delivery'] is not None and f['delivery'] >= exp['min_udp_delivery']))
        if 'max_app_duplicates' in exp:
            checks.append((f'{f["flow"]} UDP duplicates == 0', f['app_duplicates'] <= exp['max_app_duplicates']))
        checks.append((f'{f["flow"]} no corrupt datagrams', f['corrupt'] == 0))
    if exp.get('bulk_complete'):
        checks.append(('TCP bulk transfers present', bool(bulk)))
        for b in bulk:
            checks.append((f'TCP bulk {b["src"]}->{b["dst"]}:{b["port"]} complete with matching SHA-256',
                           b.get('received') == b['bytes'] and b.get('digest_ok') is True))
    if exp.get('rr_complete'):
        checks.append(('TCP request/response present', bool(rr)))
        for r in rr:
            checks.append((f'TCP rr {r["src"]}->{r["dst"]}:{r["port"]} {r.get("completed")}/{r["count"]} exchanges, none mismatched',
                           r.get('completed') == r['count'] and r.get('mismatched') == 0 and 'error' not in r))
    sends = {n: read_send_log(outdir / 'logs', n) for n in nodes}
    links, device, invalid_headers = common_checks(nodes, results, sends, exp, checks)
    capacity = None
    if sc.get('bulk_bound'):
        capacity = {}
        for b in bulk:
            send = sends[b['src']]['rows'] if sends[b['src']] is not None else None
            if send is None or 'goodput_kbps' not in b:
                continue
            data = send[(send['nexthop'] == b['dst']) & (send['data_len'] > 0)]
            big = data[data['data_len'] >= 1400]
            if not len(big):
                continue
            ns = float(sum(int(x) for x in big['nsamples']))/len(big)
            fs = results[b['src']]['tx_rate']
            per_slot = int((0.035 - 0.010)*fs // ns)
            # TCP payload per full-size segment: MTU 1500 - 20 IP - 32 TCP (timestamps)
            bound = per_slot*1448*8/(0.035*len(nodes))
            capacity[f'{b["src"]}->{b["dst"]}'] = {
                'samples_per_frame': ns, 'frames_per_slot': per_slot, 'tdma_bound_kbps': bound/1e3,
                'goodput_kbps': b['goodput_kbps'], 'efficiency': b['goodput_kbps']*1e3/bound if bound else None}
    return {
        'scenario': scenario, 'doc': sc['doc'], 'arq': sc['arq'], 'tap': True,
        'links': {str(k): v for k, v in sc['links'].items()},
        'flows': flows, 'tcp_bulk': bulk, 'tcp_rr': rr, 'capacity': capacity,
        'device': {str(k): v for k, v in device.items()},
        'links_accounting': links, 'invalid_headers': {str(k): v for k, v in invalid_headers.items()}, 'air': air,
        'checks': [{'check': c, 'passed': bool(ok)} for c, ok in checks],
        'passed': all(ok for _, ok in checks),
    }


def run(args, scenario):
    outdir = Path(args.outdir) / scenario
    if outdir.exists():
        shutil.rmtree(outdir)
    outdir.mkdir(parents=True)
    sock = str(outdir / 'air.sock')
    if len(sock) > 100:
        sock = f'/tmp/sim-air-{os.getpid()}-{scenario}.sock'
    env = dict(os.environ)
    air = subprocess.Popen([sys.executable, str(ROOT / 'scripts' / 'sim-air.py'), sock,
                            '--stats', str(outdir / 'air.json'),
                            '--burst-log', str(outdir / 'bursts.csv')],
                           stdout=open(outdir / 'air.log', 'w'), stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.05)
    nodes = list(range(1, args.nodes + 1))
    procs = {}
    tap = SCENARIOS[scenario].get('tap', False)
    start_at = time.monotonic() + TAP_STARTUP + args.warmup
    for n in nodes:
        if tap:
            script = outdir / f'node{n}.sh'
            script.write_text(tap_node_script(args, scenario, n, sock, outdir, start_at))
            cmd = ['unshare', '-rn', '--kill-child', 'sh', str(script)]
        else:
            cmd = node_command(args, scenario, n, sock, outdir)
        (outdir / f'node{n}.cmd').write_text(' '.join(cmd) + '\n')
        procs[n] = subprocess.Popen(cmd, stdout=open(outdir / f'node{n}.log', 'w'),
                                    stderr=subprocess.STDOUT, env=env, cwd=str(ROOT))
    deadline = time.monotonic() + args.warmup + args.duration + args.drain + args.timeout
    if tap:
        deadline += TAP_STARTUP + 3*args.drain + 2
    status = {}
    for n, p in procs.items():
        try:
            status[n] = p.wait(timeout=max(1.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            p.send_signal(signal.SIGINT)
            try:
                status[n] = p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
                status[n] = 'killed'
    air.send_signal(signal.SIGTERM)
    air.wait(timeout=10)
    results = {}
    for n in nodes:
        path = outdir / f'node{n}.json'
        if path.exists():
            results[n] = json.loads(path.read_text())
    if len(results) != len(nodes):
        summary = {'scenario': scenario, 'passed': False, 'error': 'missing node results',
                   'exit': {str(k): v for k, v in status.items()}}
    else:
        air_stats = json.loads((outdir / 'air.json').read_text()) if (outdir / 'air.json').exists() else {}
        if tap:
            apps = {n: json.loads((outdir / f'app{n}.json').read_text())
                    for n in nodes if (outdir / f'app{n}.json').exists()}
            summary = evaluate_tap(scenario, outdir, results, apps, air_stats, nodes)
        else:
            summary = evaluate(scenario, outdir, results, air_stats, nodes)
        summary['exit'] = {str(k): v for k, v in status.items()}
    (outdir / 'summary.json').write_text(json.dumps(summary, indent=1))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dragonradio', default=str(ROOT / 'dragonradio'), help='radio executable')
    parser.add_argument('--outdir', required=True)
    parser.add_argument('--scenario', action='append', choices=sorted(SCENARIOS), help='scenario (default: all)')
    parser.add_argument('--nodes', type=int, default=2)
    parser.add_argument('--bandwidth', type=float, default=1e6)
    parser.add_argument('--noise-db', type=float, default=-50.0)
    parser.add_argument('--warmup', type=float, default=4.0)
    parser.add_argument('--duration', type=float, default=20.0)
    parser.add_argument('--drain', type=float, default=4.0)
    parser.add_argument('--timeout', type=float, default=40.0, help='extra seconds before nodes are killed')
    parser.add_argument('extra', nargs='*', help='extra radio arguments (after --)')
    args = parser.parse_args()
    scenarios = args.scenario or list(SCENARIOS)
    summaries = []
    for sc in scenarios:
        print(f'== {sc}: {SCENARIOS[sc]["doc"]}', flush=True)
        s = run(args, sc)
        summaries.append(s)
        for f in s.get('flows', []):
            extra = (f'(SYN redelivery {f["syn_redeliveries"]})  ' if 'syn_redeliveries' in f else '')
            goodput = f'goodput {f["goodput_kbps"]:.1f} kb/s  ' if 'goodput_kbps' in f else ''
            print(f'   {f["flow"]:>14}  sent {f["sent"]:5d}  delivered {f["delivered"]:5d}  '
                  f'delivery {f["delivery"] if f["delivery"] is None else round(f["delivery"], 4)}  '
                  f'dup {f["app_duplicates"]} {extra}corrupt {f["corrupt"]}  '
                  f'{goodput}p50 {"-" if f["latency_p50_ms"] is None else round(f["latency_p50_ms"], 1)} ms', flush=True)
        for b in s.get('tcp_bulk', []):
            print(f'   TCP bulk {b["src"]}->{b["dst"]}:{b["port"]}  {b.get("received")}/{b["bytes"]} bytes  '
                  f'digest {"ok" if b.get("digest_ok") else "MISMATCH/none"}  '
                  f'{b.get("complete_s", float("nan")):.1f} s  goodput {b.get("goodput_kbps", float("nan")):.1f} kb/s'
                  + (f'  error {b["error"]}' if 'error' in b else ''), flush=True)
        for r in s.get('tcp_rr', []):
            p50 = r.get('rtt_p50')
            p95 = r.get('rtt_p95')
            print(f'   TCP rr {r["src"]}->{r["dst"]}:{r["port"]}  {r.get("completed")}/{r["count"]} exchanges  '
                  f'RTT p50 {"-" if p50 is None else round(1e3*p50, 1)} ms  p95 {"-" if p95 is None else round(1e3*p95, 1)} ms'
                  + (f'  error {r["error"]}' if 'error' in r else ''), flush=True)
        for name, c in (s.get('capacity') or {}).items():
            print(f'   capacity {name}: {c["frames_per_slot"]} frames/slot of {c["samples_per_frame"]:.0f} samples; '
                  f'TDMA bound {c["tdma_bound_kbps"]:.1f} kb/s; goodput {c["goodput_kbps"]:.1f} kb/s ({100*c["efficiency"]:.1f}%)', flush=True)
        for name, l in s.get('links_accounting', {}).items():
            print(f'   link {name}: frames sent {l.get("frames_sent")} received {l.get("frames_received")}; '
                  f'data attempts {l.get("data_attempts")} (retx {l.get("retransmissions")}) received valid {l.get("data_received_valid")}; '
                  f'bad payload {l.get("payload_invalid")}; drops {l.get("queue_or_deadline_drops")}', flush=True)
        if s.get('invalid_headers'):
            print(f'   invalid headers by receiver: {s["invalid_headers"]}', flush=True)
        for c in s.get('checks', []):
            if not c['passed']:
                print(f'   FAILED: {c["check"]}', flush=True)
        print(f'   {"PASSED" if s["passed"] else "FAILED"}', flush=True)
    (Path(args.outdir) / 'summary.json').write_text(json.dumps(summaries, indent=1))
    return 0 if all(s['passed'] for s in summaries) else 1


if __name__ == '__main__':
    raise SystemExit(main())
