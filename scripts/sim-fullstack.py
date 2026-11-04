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
import copy
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
    'jitter': dict(
        doc='Bidirectional; 0-2 ms uniform per-burst timing jitter and 50-sample delay both ways, ARQ on',
        arq=True,
        flows={1: [(2, 5001, 40, 500)], 2: [(1, 5002, 40, 500)]},
        links={2: [(1, 0.0, 0.0, 50, 0.0, 2000)], 1: [(2, 0.0, 0.0, 50, 0.0, 2000)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0, min_frame_delivery=0.999)),
    'taildrop-saturation': dict(
        doc='Saturation with a 15000-byte tail-drop queue: bounded latency, full goodput',
        arq=True, radio_args=['--taildrop', '--set', 'tail_drop_max_size=15000'],
        flows={1: [(2, 5001, 150, 1000)]}, links={},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_p95_latency_ms=800, min_efficiency=0.9),
        capacity=True),
    'red-saturation': dict(
        doc='Saturation with the RED queue at its defaults (7.5-22.5 kB, w_q 0.002); latency reported, not bounded',
        arq=True, radio_args=['--red'],
        flows={1: [(2, 5001, 150, 1000)]}, links={},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, min_efficiency=0.85),
        capacity=True),
    'red-tuned': dict(
        doc='Saturation with RED and w_q 0.05 (average tracks the queue at this rate): bounded latency',
        arq=True, radio_args=['--red', '--set', 'red_w_q=0.05'],
        flows={1: [(2, 5001, 150, 1000)]}, links={},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_p95_latency_ms=1500, min_efficiency=0.85),
        capacity=True),
    'mandate-expiry': dict(
        doc='Saturation with the mandate queue and a 300 ms latency mandate: late packets expire',
        arq=True, radio_args=['--queue', 'mandate'],
        mandates={1: [(5001, 0.3)], 2: [(5001, 0.3)]},
        flows={1: [(2, 5001, 150, 1000)]}, links={},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_latency_ms=500, min_efficiency=0.8),
        capacity=True),
    'grant-change': dict(
        doc='Timed TDMA grants: [1,2] then [1,1,2] then [1,2,2], both directions saturated',
        arq=True, duration=24.0,
        flows={1: [(2, 5001, 150, 1000)], 2: [(1, 5002, 150, 1000)]},
        links={},
        schedules=[(8.0, [1, 1, 2]), (16.0, [1, 2, 2])],
        # Schedules have no common activation time, so nodes switch tens of
        # milliseconds apart and a few frames may collide; ARQ recovers them.
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.99)),
    'three-node': dict(
        doc='Three nodes, TDMA 3 slots: unicast ring 1->2->3->1 and a broadcast from 2, ARQ on',
        arq=True, nodes=3,
        flows={1: [(2, 5001, 20, 500)], 2: [(3, 5002, 20, 500), (255, 5009, 5, 200)], 3: [(1, 5003, 20, 500)]},
        links={},
        expect=dict(min_delivery=0.995, min_broadcast_delivery=0.99, max_app_duplicates=0, min_frame_delivery=0.999)),
    'three-node-erasure': dict(
        doc='Three nodes, erasure 15% on 1->2 and 3->1, 30% on 2->3; ARQ recovers unicast',
        arq=True, nodes=3,
        flows={1: [(2, 5001, 20, 500)], 2: [(3, 5002, 20, 500), (255, 5009, 5, 200)], 3: [(1, 5003, 20, 500)]},
        links={2: [(1, 0.0, 0.0, 0, 0.15)], 1: [(3, 0.0, 0.0, 0, 0.15)], 3: [(2, 0.0, 0.0, 0, 0.3)]},
        expect=dict(min_delivery=0.995, max_app_duplicates=0)),
}

# Independent node clocks (P4 item 2.2). Node 2's device clock is ahead of
# node 1's by more than the 10 ms TDMA guard, and every front end couples its
# own transmissions into its receiver (-30 dB, so a node cannot hear a peer
# while it transmits). Both directions are saturated (tail-drop queue) so that
# every slot is filled to its end: misaligned slots then overlap. Without synchronization, node 2's slots overlap node
# 1's; with the radio's timestamp protocol (HELLO timestamps, node 1 as time
# master, radio.synchronizeClock every 2 s) their WallClocks align.
_LEAK = ['--sim-tx-leakage-db', '-30']
_SYNC = ['--time-master', '1', '--hello-period', '0.5', '--timesync-period', '2']
_SAT = {1: [(2, 5001, 150, 1000)], 2: [(1, 5002, 150, 1000)]}
_TAIL = ['--taildrop', '--set', 'tail_drop_max_size=15000']
SCENARIOS.update({
    'clock-offset-nosync': dict(
        doc='Node 2 clock +15 ms (guard 10 ms), TX leakage, no synchronization: overlapping slots lose frames',
        arq=True, warmup=12.0, flows=_SAT, links={},
        radio_args=_LEAK + _TAIL, node_args={2: ['--sim-clock-offset', '0.015']},
        expect=dict(max_frame_delivery=0.95, max_app_duplicates=0)),
    'clock-offset-sync': dict(
        doc='Same offset with the timestamp protocol: WallClocks align and every frame is received',
        arq=True, warmup=12.0, flows=_SAT, links={},
        radio_args=_LEAK + _TAIL + _SYNC, node_args={2: ['--sim-clock-offset', '0.015']},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_sync_error_ms=1.0)),
    'clock-skew-sync': dict(
        doc='Node 2 clock +15 ms and 20 ppm fast (carrier and sample clock too), synchronized',
        arq=True, warmup=12.0, flows=_SAT, links={},
        radio_args=_LEAK + _TAIL + _SYNC, node_args={2: ['--sim-clock-offset', '0.015', '--sim-clock-ppm', '20']},
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_sync_error_ms=1.0)),
})

# Downlink OFDMA (P5). Node 1 is the base station; nodes 2 and 3 are UEs.
# TDMA gives each node one slot, so UE feedback (ACKs) is time separated in
# the UEs' own slots. Node 1's bursts carry one packet for each UE on half the
# tones when both have traffic (native-ofdma), otherwise one packet on every
# tone. rs8 QPSK, ARQ, tail-drop queue.
_OFDMA = ['--phy', 'native-ofdma', '-c', 'rs8', '-k', 'none', '-m', 'qpsk', '--taildrop', '--set', 'tail_drop_max_size=15000']
_DL_SAT = {1: [(2, 5002, 75, 1000), (3, 5003, 75, 1000)]}
SCENARIOS.update({
    'ofdma-both-strong': dict(
        doc='Downlink OFDMA to two strong UEs, both saturated',
        arq=True, nodes=3, flows=_DL_SAT, links={}, radio_args=_OFDMA, ofdma=True,
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, min_shared_bursts=0.5)),
    'ofdma-strong-weak': dict(
        doc='Same bursts; UE 3 weak (link -33 dB), UE 2 strong; retry budget 3 per packet',
        arq=True, nodes=3, flows=_DL_SAT, links={3: [(1, -33.0, 0.0, 0, 0.0)]},
        radio_args=_OFDMA + ['--max-retransmissions', '3'], ofdma=True,
        expect=dict(max_app_duplicates=0, strong_unaffected=(2, 0.999))),
    'ofdma-one-idle': dict(
        doc='Only UE 2 has traffic: single-user bursts on every tone',
        arq=True, nodes=3, flows={1: [(2, 5002, 150, 1000)]}, links={}, radio_args=_OFDMA, ofdma=True,
        expect=dict(max_app_duplicates=0, min_frame_delivery=0.999, max_shared_bursts=0.0)),
    'ofdma-one-disconnected': dict(
        doc='UE 3 never hears node 1 and is never heard: UE 2 keeps its service',
        arq=True, nodes=3, flows=_DL_SAT, links={3: [(1, 0.0, 0.0, 0, 1.0)], 1: [(3, 0.0, 0.0, 0, 1.0)]},
        radio_args=_OFDMA, ofdma=True,
        expect=dict(max_app_duplicates=0, strong_unaffected=(2, 0.999))),
    'ofdma-unequal': dict(
        doc='Allocation 1:2 (UE 2 a third of the tones): UE 2 at 5 pkt/s, UE 3 saturated; per-flow queue (mandate)',
        arq=True, nodes=3, flows={1: [(2, 5002, 5, 1000), (3, 5003, 150, 1000)]}, links={},
        radio_args=_OFDMA + ['--ofdma-allocation', '1:2', '--queue', 'mandate'], ofdma=True,
        expect=dict(min_delivery_flow=(2, 0.995), max_app_duplicates=0, min_frame_delivery=0.999)),
})

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
    'tap-three-node': dict(
        doc='Three nodes over tap: TCP bulk 1->2, TCP request/response 2->3 and UDP 3->1 at once',
        arq=True, nodes=3, links={},
        apps={1: ['--tcp-bulk', f'{IP % 2}:6001:200000', '--udp-recv', '5003'],
              2: ['--tcp-server', '6001', '--tcp-rr', f'{IP % 3}:6002:60:64'],
              3: ['--tcp-server', '6002', '--udp-send', f'{IP % 1}:5003:10:300']},
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
    for link in sc['links'].get(node, []):
        cmd += ['--link', ':'.join(str(v) for v in link)]
    for (port, latency) in sc.get('mandates', {}).get(node, []):
        cmd += ['--mandate', f'{port}:{latency}']
    # Timed grants: offsets from the estimated traffic start, the same
    # absolute host monotonic time on every node.
    for (offset, slots) in sc.get('schedules', []):
        cmd += ['--schedule-at', f'{args.schedule_base + offset:.6f}:' + ','.join(str(n) for n in slots)]
    node_extra = [a for (n, extra) in (args.node_extra or []) if int(n) == node for a in shlex.split(extra)]
    return cmd + sc.get('radio_args', []) + sc.get('node_args', {}).get(node, []) + args.extra + node_extra


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


def to_host(result, t):
    """Host monotonic time of a node's device time t (independent clocks)."""
    c = result.get('clock') or {}
    return (t - c.get('sim_offset', 0.0))/(1 + 1e-6*c.get('sim_ppm', 0.0))


def sync_error(results, nodes):
    """WallClock of each node minus the time master's at the same host times,
    from 1 Hz clock samples; returns per node the max |error| (ms) over the
    second half of the run and the last error."""
    master = next((results[n]['clock']['time_master'] for n in nodes if (results[n].get('clock') or {}).get('time_master')), None)
    if master is None or master not in results:
        return None
    ms = [s for s in results[master]['clock']['samples'] if 'event' not in s]
    if len(ms) < 2:
        return None
    out = {}
    for n in nodes:
        if n == master:
            continue
        samples = [s for s in results[n]['clock']['samples'] if 'event' not in s]
        errs = []
        for s in samples:
            # Master's wall clock at this host time (linear between samples).
            j = next((k for k in range(1, len(ms)) if ms[k]['host'] >= s['host']), None)
            if j is None:
                continue
            a, b = ms[j - 1], ms[j]
            w = a['wall'] + (b['wall'] - a['wall'])*(s['host'] - a['host'])/(b['host'] - a['host'])
            errs.append((s['host'], 1e3*(s['wall'] - w)))
        if errs:
            half = errs[len(errs)//2:]
            out[str(n)] = {'max_abs_ms_second_half': max(abs(e) for _, e in half), 'last_ms': errs[-1][1],
                           'first_ms': errs[0][1], 'series_ms': [round(e, 4) for _, e in errs]}
    return out


def link_accounting(send, frames, src, dst, snapshot, src_result=None, dst_result=None):
    """Per-attempt accounting for frames from src to dst.

    send: src's production send log; frames: dst's RxRecorder records taken at
    host monotonic time snapshot.
    """
    import numpy as np
    out = {}
    if send is not None:
        rows = send['rows']
        t_send = rows['mono_timestamp'] + send['origin']
        if src_result is not None:
            t_send = to_host(src_result, t_send)
        rows = rows[t_send <= snapshot - IN_FLIGHT]
        logged = rows[(rows['nexthop'] == dst) | (rows['nexthop'] == 255)]
        # Packets the queue or ARQ dropped are logged too but never reached
        # the air; count them separately.
        to_dst = logged[logged['dropped'] == 0]
        data = to_dst[to_dst['data_len'] > 0]
        out['frames_sent'] = int(len(to_dst))
        out['data_attempts'] = int(len(data))
        out['data_first_attempts'] = int(np.sum(data['nretrans'] == 0))
        out['retransmissions'] = int(np.sum(data['nretrans'] > 0))
        out['queue_or_deadline_drops'] = int(np.sum(logged['dropped'] != 0))
    rx_time = (lambda t: to_host(dst_result, t)) if dst_result is not None else (lambda t: t)
    from_src = [r for r in frames if r[1] == src and rx_time(r[0]) <= snapshot - IN_FLIGHT]
    out['frames_received'] = len(from_src)
    out['data_received_valid'] = sum(1 for r in from_src if r[6] and r[7] > 0)
    out['payload_invalid'] = sum(1 for r in from_src if r[5] and not r[6])
    if 'frames_sent' in out and out['frames_sent']:
        out['frame_delivery'] = sum(1 for r in from_src if r[6])/out['frames_sent']
    if out.get('data_attempts'):
        out['data_attempt_delivery'] = out['data_received_valid']/out['data_attempts']
    return out


def tdma_bound(ns, fs, size, nslots, slots_owned=1, slot=0.035, guard=0.010):
    """Application bits/s a node can send in slots_owned of nslots TDMA slots.

    SlotSynthesizer::push accepts a frame only if it ends within the slot minus
    guard (superslots off), so a slot carries whole frames only.
    """
    per_slot = int((slot - guard)*fs // ns)
    return per_slot, per_slot*size*8*slots_owned/(slot*nslots)


def window_kbps(arrivals, size, lo, hi):
    if hi <= lo:
        return None
    return 8*size*sum(1 for t in arrivals if lo <= t < hi)/(hi - lo)/1e3


def evaluate(scenario, outdir, results, air, nodes):
    sc = SCENARIOS[scenario]
    exp = sc['expect']
    flows = []
    for src, fl in sc['flows'].items():
        for (dest, port, pps, size) in fl:
            sent = next((f['sent'] for f in results[src]['flows'] if f['port'] == port), 0)
            broadcast = dest == 255
            for dst in ([n for n in nodes if n != src] if broadcast else [dest]):
                rec = results[dst]['received'].get(f'{src}:{port}', {})
                unique = rec.get('unique', 0)
                seqs = set(rec.get('seqs', []))
                # Diagnostic: duplicates of app seq 0 seen with repeated SYNs
                # (redelivered before the receive-window fix).
                syns = sum(1 for r in results[dst].get('rx_frames', []) if r[1] == src and r[4] & 1 and r[6])
                dups = rec.get('dup_seqs', [])
                flows.append({
                    'flow': f'{src}->{dst}:{port}' + (' bcast' if broadcast else ''),
                    'src': src, 'dst': dst, 'port': port, 'broadcast': broadcast,
                    'offered_pps': pps, 'size': size,
                    'sent': sent, 'delivered': unique,
                    'delivery': unique/sent if sent else None,
                    'missing': len(set(range(sent)) - seqs),
                    'app_duplicates': rec.get('duplicates', 0),
                    'app_duplicate_seqs': dups,
                    'syn_frames_received': syns,
                    'syn_redeliveries': min(dups.count(0), max(0, syns - 1)),
                    'corrupt': rec.get('corrupt', 0),
                    'out_of_order': rec.get('out_of_order', 0),
                    # Over the arrival window: a saturated flow keeps draining
                    # its queue after the traffic generator stops.
                    'goodput_kbps': (8*rec['bytes']/(rec['last_rx'] - rec['first_rx'])/1e3
                                     if unique > 1 and rec['last_rx'] > rec['first_rx'] else 0.0),
                    'latency_p50_ms': None if rec.get('latency_p50') is None else 1e3*rec['latency_p50'],
                    'latency_p95_ms': None if rec.get('latency_p95') is None else 1e3*rec['latency_p95'],
                    'latency_max_ms': None if rec.get('latency_max') is None else 1e3*rec['latency_max'],
                    '_arrivals': rec.get('arrivals', []),
                })
    checks = []
    sends = {n: read_send_log(outdir / 'logs', n) for n in nodes}
    for f in flows:
        key = 'min_broadcast_delivery' if f['broadcast'] else 'min_delivery'
        if key in exp:
            checks.append((f'{f["flow"]} delivery >= {exp[key]}', f['delivery'] is not None and f['delivery'] >= exp[key]))
        if 'max_delivery' in exp and not f['broadcast']:
            checks.append((f'{f["flow"]} delivery <= {exp["max_delivery"]}', f['delivery'] is not None and f['delivery'] <= exp['max_delivery']))
        if 'max_app_duplicates' in exp:
            checks.append((f'{f["flow"]} application duplicates == 0', f['app_duplicates'] <= exp['max_app_duplicates']))
        checks.append((f'{f["flow"]} no corrupt payloads', f['corrupt'] == 0))
        if 'max_latency_ms' in exp:
            checks.append((f'{f["flow"]} max latency {f["latency_max_ms"]} ms <= {exp["max_latency_ms"]} ms',
                           f['latency_max_ms'] is not None and f['latency_max_ms'] <= exp['max_latency_ms']))
        if 'max_p95_latency_ms' in exp:
            checks.append((f'{f["flow"]} p95 latency {f["latency_p95_ms"]} ms <= {exp["max_p95_latency_ms"]} ms',
                           f['latency_p95_ms'] is not None and f['latency_p95_ms'] <= exp['max_p95_latency_ms']))
    if 'min_retransmissions' in exp:
        retx = 0
        for src in nodes:
            send = sends[src]
            if send is not None:
                rows = send['rows']
                retx += int(((rows['data_len'] > 0) & (rows['nretrans'] > 0)).sum())
        checks.append((f'retransmissions >= {exp["min_retransmissions"]} (logged {retx})',
                       all(s is not None for s in sends.values()) and retx >= exp['min_retransmissions']))
    links, device, invalid_headers = common_checks(nodes, results, sends, exp, checks)

    ofdma = None
    if sc.get('ofdma'):
        # Bursts from node 1 that carried two packets: send-log rows sharing a
        # transmission timestamp (one burst) with different next hops.
        ofdma = {}
        send1 = sends.get(1)
        if send1 is not None:
            rows = send1['rows']
            rows = rows[(rows['dropped'] == 0) & (rows['nexthop'] != 255)]
            from collections import defaultdict
            by_t = defaultdict(set)
            for r in rows:
                by_t[float(r['timestamp'])].add(int(r['nexthop']))
            shared = sum(1 for v in by_t.values() if len(v) > 1)
            ofdma = {'bursts': len(by_t), 'shared_bursts': shared, 'packets': int(len(rows)),
                     'shared_fraction_of_packets': (2*shared/len(rows)) if len(rows) else None}
        frac = ofdma.get('shared_fraction_of_packets') or 0.0
        if 'min_shared_bursts' in exp:
            checks.append((f'node 1: {100*frac:.1f}% of packets shared a burst (>= {100*exp["min_shared_bursts"]:.0f}%)',
                           frac >= exp['min_shared_bursts']))
        if 'max_shared_bursts' in exp:
            checks.append((f'node 1: {100*frac:.1f}% of packets shared a burst (<= {100*exp["max_shared_bursts"]:.0f}%)',
                           frac <= exp['max_shared_bursts']))
        if 'strong_unaffected' in exp:
            ue, thresh = exp['strong_unaffected']
            l = links.get(f'1->{ue}', {})
            checks.append((f'UE {ue} frame delivery {l.get("frame_delivery", 0):.4f} >= {thresh} despite the other UE',
                           l.get('frame_delivery', 0) >= thresh))
        if 'min_delivery_flow' in exp:
            ue, thresh = exp['min_delivery_flow']
            f = next((f for f in flows if f['dst'] == ue), None)
            checks.append((f'flow to UE {ue} delivery >= {thresh}', f is not None and (f['delivery'] or 0) >= thresh))

    sync = sync_error(results, nodes)
    if 'max_sync_error_ms' in exp:
        for n, e in (sync or {}).items():
            checks.append((f'node {n} WallClock within {exp["max_sync_error_ms"]} ms of the time master over the second half '
                           f'(max {e["max_abs_ms_second_half"]:.3f} ms; first {e["first_ms"]:.3f} ms)',
                           e['max_abs_ms_second_half'] <= exp['max_sync_error_ms']))
        checks.append(('sync error measured', bool(sync)))

    capacity = None
    if sc.get('capacity'):
        capacity = {}
        for f in flows:
            send = sends[f['src']]['rows'] if sends[f['src']] is not None else None
            data = (send[(send['nexthop'] == f['dst']) & (send['data_len'] > 0) & (send['dropped'] == 0)]
                    if send is not None else [])
            if not len(data):
                continue
            ns = float(sum(int(x) for x in data['nsamples']))/len(data)
            per_slot, bound = tdma_bound(ns, results[f['src']]['tx_rate'], f['size'], len(nodes))
            capacity[f'{f["src"]}->{f["dst"]}'] = {
                'samples_per_frame': ns, 'frames_per_slot': per_slot,
                'tdma_bound_kbps': bound/1e3, 'goodput_kbps': f['goodput_kbps'],
                'efficiency': f['goodput_kbps']*1e3/bound if bound else None}
            if 'min_efficiency' in exp:
                checks.append((f'{f["flow"]} goodput >= {exp["min_efficiency"]} of the TDMA bound',
                               bound > 0 and f['goodput_kbps']*1e3/bound >= exp['min_efficiency']))

    # Timed grants: goodput of each saturated flow between schedule changes,
    # compared with each node's share of the slots.
    grants = None
    if sc.get('schedules'):
        applied = []
        for i in range(len(sc['schedules'])):
            times = [r['schedules'][i]['applied'] for r in results.values() if len(r.get('schedules', [])) > i]
            checks.append((f'schedule {i} applied on every node', len(times) == len(nodes)))
            applied.append(max(times) if times else None)
            if len(times) == len(nodes):
                checks.append((f'schedule {i} applied on all nodes within 200 ms (skew {1e3*(max(times) - min(times)):.1f} ms)',
                               max(times) - min(times) < 0.2))
        settle = 1.5
        first = max(min(f['_arrivals']) for f in flows if f['_arrivals']) if all(f['_arrivals'] for f in flows) else None
        last = min(max(f['_arrivals']) for f in flows if f['_arrivals']) if all(f['_arrivals'] for f in flows) else None
        edges = [first] + applied + [last]
        phases = []
        initial = sc.get('initial_slots', list(nodes))
        slot_sets = [initial] + [slots for (_off, slots) in sc['schedules']]
        for i, slots in enumerate(slot_sets):
            lo = None if edges[i] is None else edges[i] + (settle if i else 1.0)
            hi = edges[i + 1]
            phase = {'slots': slots, 'window': [lo, hi], 'flows': {}}
            for f in flows:
                k = window_kbps(f['_arrivals'], f['size'], lo, hi) if lo is not None and hi is not None else None
                share = slots.count(f['src'])/len(slots)
                phase['flows'][f['flow']] = {'goodput_kbps': k, 'slot_share': share}
            vals = [(v['goodput_kbps'], v['slot_share']) for v in phase['flows'].values()]
            if len(vals) == 2 and all(v[0] for v in vals):
                measured = vals[0][0]/vals[1][0]
                expected = vals[0][1]/vals[1][1]
                phase['ratio'] = measured
                phase['expected_ratio'] = expected
                checks.append((f'phase {i} {slots}: goodput ratio {measured:.2f} within 25% of slot ratio {expected:.2f}',
                               abs(measured/expected - 1) <= 0.25))
            else:
                checks.append((f'phase {i} {slots}: both flows measured', False))
            phases.append(phase)
        delays = {str(n): [round(e['applied'] - e['requested'], 4) for e in results[n].get('schedules', [])] for n in nodes}
        grants = {'applied': applied, 'apply_delay_s': delays, 'phases': phases}

    for f in flows:
        del f['_arrivals']
    return {
        'scenario': scenario, 'doc': sc['doc'], 'arq': sc['arq'], 'nodes': len(nodes),
        'radio_args': sc.get('radio_args', []),
        'links': {str(k): v for k, v in sc['links'].items()},
        'flows': flows, 'device': {str(k): v for k, v in device.items()},
        'links_accounting': links, 'invalid_headers': {str(k): v for k, v in invalid_headers.items()}, 'air': air,
        'capacity': capacity, 'grants': grants, 'sync': sync, 'ofdma': ofdma,
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
                                                         results[dst]['snapshot_mono'], results[src], results[dst])
    if 'min_frame_delivery' in exp:
        for name, l in links.items():
            if l.get('frames_sent'):
                checks.append((f'{name} PHY frame delivery >= {exp["min_frame_delivery"]}', l.get('frame_delivery', 0) >= exp['min_frame_delivery']))
    if 'max_frame_delivery' in exp:
        worst = min((l.get('frame_delivery', 1.0) for l in links.values() if l.get('frames_sent')), default=1.0)
        checks.append((f'worst link PHY frame delivery {worst:.3f} <= {exp["max_frame_delivery"]} (the impairment is visible)',
                       worst <= exp['max_frame_delivery']))
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
            data = send[(send['nexthop'] == b['dst']) & (send['data_len'] > 0) & (send['dropped'] == 0)]
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
    sc = SCENARIOS[scenario]
    args = copy.copy(args)
    args.nodes = sc.get('nodes', args.nodes)
    args.duration = sc.get('duration', args.duration)
    args.warmup = sc.get('warmup', args.warmup)
    # Nodes start traffic about this long after launch (radio start-up plus
    # warm-up); timed schedule changes are offsets from it.
    args.schedule_base = time.monotonic() + TAP_STARTUP + args.warmup
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
    parser.add_argument('--node-extra', action='append', nargs=2, metavar=('NODE', 'ARGS'),
                        help='extra radio arguments for one node (shell words), e.g. --node-extra 2 "--sim-clock-ppm 5"')
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
        for i, ph in enumerate((s.get('grants') or {}).get('phases', [])):
            rates = ', '.join(f'{k} {"-" if v["goodput_kbps"] is None else round(v["goodput_kbps"], 1)} kb/s (share {v["slot_share"]:.2f})'
                              for k, v in ph['flows'].items())
            print(f'   grant phase {i} slots {ph["slots"]}: {rates}; ratio {ph.get("ratio", float("nan")):.2f} '
                  f'(expected {ph.get("expected_ratio", float("nan")):.2f})', flush=True)
        for name, c in (s.get('capacity') or {}).items():
            print(f'   capacity {name}: {c["frames_per_slot"]} frames/slot of {c["samples_per_frame"]:.0f} samples; '
                  f'TDMA bound {c["tdma_bound_kbps"]:.1f} kb/s; goodput {c["goodput_kbps"]:.1f} kb/s ({100*c["efficiency"]:.1f}%)', flush=True)
        for name, l in s.get('links_accounting', {}).items():
            print(f'   link {name}: frames sent {l.get("frames_sent")} received {l.get("frames_received")}; '
                  f'data attempts {l.get("data_attempts")} (retx {l.get("retransmissions")}) received valid {l.get("data_received_valid")}; '
                  f'bad payload {l.get("payload_invalid")}; drops {l.get("queue_or_deadline_drops")}', flush=True)
        if s.get('ofdma'):
            o = s['ofdma']
            print(f'   ofdma: node 1 sent {o.get("packets")} packets in {o.get("bursts")} bursts; '
                  f'{o.get("shared_bursts")} bursts carried two UEs', flush=True)
        for n, e in (s.get('sync') or {}).items():
            print(f'   sync node {n}: WallClock error vs time master first {e["first_ms"]:.3f} ms, '
                  f'last {e["last_ms"]:.3f} ms, max over second half {e["max_abs_ms_second_half"]:.3f} ms', flush=True)
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
