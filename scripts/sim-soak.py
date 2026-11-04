#!/usr/bin/env python3
"""Real-time soak of the production radio over the simulated device (P4).

Two nodes (dragonradio scripts/sim-node.py --soak) run the production radio
at wall-clock pace over SimUSRP and scripts/sim-air.py with saturating
bidirectional synthetic UDP traffic for a long duration. Each process is
pinned to its own CPU set. A monitor samples /proc every few seconds:
per-thread CPU time (threads are named dr-*, see util/threads.hh), process
RSS and high-water mark, and voluntary/involuntary context switches.

After the run the soak is scored against the P4 real-time gate:

* CPU headroom: every radio thread at most 50% busy over the traffic window
  (headroom >= 2x). The simulated device's reader thread (dr-sim-reader)
  and traffic generator (dr-simtraffic) stand in for UHD and the tap
  device; they are reported but not part of the radio's gate.
* Device: no late TX bursts, underflows, late RX, RX overflows or queue drops
  during traffic; relay saw no late bursts or backlog drops.
* Bounded queues: network-queue depth (reconstructed from the production
  send log) p99 within the tail-drop limit.
* Service and latency tails: p50/p95/p99/p99.9 of modulation, synthesis,
  queue and demodulation latency (production HDF5 logs) and one-way
  application latency (bounded histogram in SimTraffic).
* Memory: RSS slope over the second half of the traffic window (leak check).

Usage:
  sim-soak.py --outdir DIR --phy native|liquid --rate 1 --duration 1800 \\
      --cores1 0,2,4,6,8,10 --cores2 12,14,16,18,20,22 --cores-air 24
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLK_TCK = os.sysconf('SC_CLK_TCK')
SIM_THREADS = ('dr-sim-reader', 'dr-simtraffic')
# Workers that spin while a streaming buffer fills: /proc CPU time is not their
# load; their WorkMeter (processing time only) is used instead.
SPINNING = ('dr-chan-fft', 'dr-demod', 'dr-mod')


def spinning(comm):
    return any(comm.startswith(p) for p in SPINNING)


def meter_utilization(stats, t0, t1):
    """Work-meter busy fraction per meter between the stats records nearest t0 and t1."""
    recs = [r for r in stats if 'work' in r]
    if len(recs) < 2:
        return {}
    a = min(recs, key=lambda r: abs(r['mono'] - t0))
    b = min(recs, key=lambda r: abs(r['mono'] - t1))
    dt = b['mono'] - a['mono']
    if dt <= 0:
        return {}
    return {k: (v - a['work'].get(k, 0.0))/dt for k, v in b['work'].items()}

PHYS = {
    # Production configurations (P3): rs8 payload code on QPSK. Native uses
    # its default v29 header code; liquid ships a v29p78 BPSK header.
    'native': ['--phy', 'native-ofdm', '-c', 'rs8', '-k', 'none', '-m', 'qpsk'],
    'liquid': ['--phy', 'ofdm', '-c', 'rs8', '-k', 'none', '-m', 'qpsk'],
}


def read_proc(pid):
    """Process RSS/HWM/context switches and per-thread CPU ticks and switches."""
    out = {'threads': {}}
    try:
        with open(f'/proc/{pid}/status') as f:
            for line in f:
                k, _, v = line.partition(':')
                if k in ('VmRSS', 'VmHWM'):
                    out[k] = int(v.split()[0])
                elif k in ('voluntary_ctxt_switches', 'nonvoluntary_ctxt_switches'):
                    out[k] = int(v)
        for tid in os.listdir(f'/proc/{pid}/task'):
            try:
                with open(f'/proc/{pid}/task/{tid}/stat') as f:
                    stat = f.read()
                comm = stat[stat.index('(') + 1:stat.rindex(')')]
                fields = stat[stat.rindex(')') + 2:].split()
                t = {'comm': comm, 'utime': int(fields[11]), 'stime': int(fields[12])}
                with open(f'/proc/{pid}/task/{tid}/status') as f:
                    for line in f:
                        if line.startswith('voluntary_ctxt_switches'):
                            t['vcs'] = int(line.split(':')[1])
                        elif line.startswith('nonvoluntary_ctxt_switches'):
                            t['nvcs'] = int(line.split(':')[1])
                out['threads'][tid] = t
            except (FileNotFoundError, ProcessLookupError, ValueError):
                pass
    except (FileNotFoundError, ProcessLookupError):
        return None
    return out


def percentiles(values, qs=(0.5, 0.95, 0.99, 0.999)):
    import numpy as np
    if len(values) == 0:
        return None
    v = np.asarray(values, dtype=float)
    out = {f'p{str(q * 100).rstrip("0").rstrip(".").replace(".", "")}': float(np.quantile(v, q)) for q in qs}
    out['max'] = float(v.max())
    out['mean'] = float(v.mean())
    out['n'] = int(len(v))
    return out


def thread_utilization(samples, t0, t1):
    """Per-thread busy fraction between the samples nearest t0 and t1."""
    if not samples:
        return {}
    a = min(samples, key=lambda s: abs(s['mono'] - t0))
    b = min(samples, key=lambda s: abs(s['mono'] - t1))
    dt = b['mono'] - a['mono']
    if dt <= 0:
        return {}
    out = {}
    for tid, tb in b['proc']['threads'].items():
        ta = a['proc']['threads'].get(tid)
        if ta is None:
            continue
        busy = ((tb['utime'] + tb['stime']) - (ta['utime'] + ta['stime']))/CLK_TCK/dt
        out[tid] = {'comm': tb['comm'], 'busy': busy,
                    'vcs_per_s': (tb.get('vcs', 0) - ta.get('vcs', 0))/dt,
                    'nvcs_per_s': (tb.get('nvcs', 0) - ta.get('nvcs', 0))/dt}
    return out


def rss_slope(samples, t0, t1):
    """Least-squares RSS slope (kB per minute) over [t0, t1]."""
    pts = [(s['mono'], s['proc']['VmRSS']) for s in samples if t0 <= s['mono'] <= t1 and 'VmRSS' in s['proc']]
    if len(pts) < 3:
        return None
    n = len(pts)
    mx = sum(p[0] for p in pts)/n
    my = sum(p[1] for p in pts)/n
    sxx = sum((p[0] - mx)**2 for p in pts)
    sxy = sum((p[0] - mx)*(p[1] - my) for p in pts)
    return 60*sxy/sxx if sxx else None


def read_logs(logdir, node):
    """Send and receive rows of a node's production HDF5 log."""
    import h5py
    path = Path(logdir) / f'node-{node:03d}' / 'radio.h5'
    if not path.exists():
        return None
    with h5py.File(path, 'r') as f:
        send = f['send'][:] if 'send' in f else None
        recv = f['recv'][:] if 'recv' in f else None
        origin = float(f.attrs['start'])
    if send is not None:
        send = send[send['timestamp'] != 0]
    if recv is not None:
        recv = recv[recv['timestamp'] != 0]
    return {'send': send, 'recv': recv, 'origin': origin}


def queue_depth(send, origin, t0, t1, step=0.01):
    """Network-queue occupancy (packets) sampled every `step` s in [t0, t1].

    A packet counts from its arrival from the network (net_timestamp) until
    the controller pulls it for the synthesizer (net + llc_latency). Only
    MandateQueue records enqueue/dequeue times, so llc_latency is the queue
    wait that every queue type logs. First transmissions only: ARQ
    retransmissions re-enter the controller and are not counted.
    """
    import numpy as np
    rows = send[(send['nretrans'] == 0) & (send['dropped'] == 0) & (send['data_len'] > 0)]
    enq = origin + rows['net_timestamp']
    deq = origin + rows['net_timestamp'] + rows['llc_latency']
    grid = np.arange(t0, t1, step)
    if not len(grid):
        return None
    depth = np.searchsorted(np.sort(enq), grid, side='right') - np.searchsorted(np.sort(deq), grid, side='right')
    return percentiles(depth)


def node_command(args, node, outdir, rate, pps, queue_bytes):
    peer = 2 if node == 1 else 1
    cmd = ['taskset', '-c', args.cores1 if node == 1 else args.cores2,
           args.dragonradio, str(ROOT / 'scripts' / 'sim-node.py'),
           '-i', str(node), '-n', '2', '--sim-air', args.sock, '--sim-traffic',
           '--sim-noise-db', str(args.noise_db), '-b', str(rate)]
    cmd += PHYS[args.phy]
    cmd += ['-l', str(outdir / 'logs'), '--arq',
            '--taildrop', '--set', f'tail_drop_max_size={queue_bytes}',
            '--warmup', str(args.warmup), '--duration', str(args.duration), '--drain', str(args.drain),
            '--soak', '--stats-file', str(outdir / f'stats{node}.jsonl'), '--stats-interval', str(args.interval),
            '--results', str(outdir / f'node{node}.json'),
            '--flow', f'{peer}:{5000 + node}:{pps}:{args.size}']
    return cmd + args.extra


def summarize(args, outdir, procs, monitor, rate, pps, queue_bytes, t_launch):
    results = {}
    for n in (1, 2):
        p = outdir / f'node{n}.json'
        if p.exists():
            results[n] = json.loads(p.read_text())
    air = json.loads((outdir / 'air.json').read_text()) if (outdir / 'air.json').exists() else {}
    summary = {'phy': args.phy, 'rate_msps': rate / 1e6, 'duration_s': args.duration, 'offered_pps': pps,
               'payload_bytes': args.size, 'queue_bytes': queue_bytes, 'cores': {'1': args.cores1, '2': args.cores2,
               'air': args.cores_air}, 'radio_args': PHYS[args.phy] + args.extra, 'air': air, 'nodes': {},
               'checks': []}
    checks = summary['checks']
    checks.append(('both node results present', len(results) == 2))
    for n, res in results.items():
        node = {}
        # Traffic window on the host monotonic clock: from the per-second
        # arrival counts' origin (first arrival) to the stats snapshots.
        stats = [json.loads(l) for l in open(outdir / f'stats{n}.jsonl')] if (outdir / f'stats{n}.jsonl').exists() else []
        t_traffic0 = stats[0]['mono'] if stats else None
        t_traffic1 = t_traffic0 + args.duration if stats else None
        node['traffic_window'] = [t_traffic0, t_traffic1]
        samples = [s for s in monitor if s['node'] == n and s['proc']]
        # Utilization over the traffic window, skipping the first minute (or 10%).
        skip = min(60.0, 0.1 * args.duration)
        if t_traffic0 is not None:
            util = thread_utilization(samples, t_traffic0 + skip, t_traffic1)
        else:
            util = {}
        by_comm = {}
        for t in util.values():
            c = by_comm.setdefault(t['comm'], {'threads': 0, 'busy_max': 0.0, 'busy_sum': 0.0,
                                               'vcs_per_s': 0.0, 'nvcs_per_s': 0.0})
            c['threads'] += 1
            c['busy_max'] = max(c['busy_max'], t['busy'])
            c['busy_sum'] += t['busy']
            c['vcs_per_s'] += t['vcs_per_s']
            c['nvcs_per_s'] += t['nvcs_per_s']
        node['threads'] = by_comm
        # Load per radio thread: work meters for the spinning workers, /proc
        # CPU time for threads that block when idle.
        meters = meter_utilization(stats, t_traffic0 + skip, t_traffic1) if t_traffic0 is not None else {}
        node['work_utilization'] = meters
        node['sim_device_busy'] = {k: by_comm[k]['busy_max'] for k in SIM_THREADS if k in by_comm}
        load = {k: v['busy_max'] for k, v in by_comm.items() if k not in SIM_THREADS and not spinning(k)}
        load.update({k: v for k, v in meters.items() if k != 'dr-sim-render'})
        # The MAC RX thread also runs the simulated device's rendering
        # (noise and link model) inside burstRX; that is device cost, not
        # radio work. Single RX stream per node, so one render meter.
        if 'dr-mac-rx' in load and 'dr-sim-render' in meters:
            node['mac_rx_busy_including_device_model'] = load['dr-mac-rx']
            load['dr-mac-rx'] = max(0.0, load['dr-mac-rx'] - meters['dr-sim-render'])
            node['sim_device_busy']['dr-sim-render'] = meters['dr-sim-render']
        node['thread_load'] = load
        busiest = max(load.items(), key=lambda kv: kv[1]) if load else (None, 0.0)
        node['busiest_radio_thread'] = busiest[0]
        node['busiest_radio_busy'] = busiest[1]
        node['headroom'] = 1/busiest[1] if busiest[1] > 0 else None
        node['process_cpus'] = sum(v['busy_sum'] for v in by_comm.values())
        node['spinning_cpus'] = sum(v['busy_sum'] for k, v in by_comm.items() if spinning(k))
        node['radio_work_cpus'] = sum(load.values())
        checks.append((f'node {n}: busiest radio thread {busiest[0]} {100*busiest[1]:.1f}% busy <= 50% (headroom >= 2x)',
                       busiest[1] <= 0.5 and bool(meters)))
        for k, b in node['sim_device_busy'].items():
            checks.append((f'node {n}: simulated device thread {k} {100*b:.1f}% busy < 90% (otherwise the device, not the radio, limits the run)', b < 0.9))
        # Memory.
        rss = [(s['mono'], s['proc'].get('VmRSS'), s['proc'].get('VmHWM')) for s in samples if 'VmRSS' in s['proc']]
        node['rss_kb'] = [[round(t - t_launch, 1), r] for t, r, _ in rss]
        node['rss_hwm_kb'] = max((h for _, _, h in rss), default=None)
        if t_traffic0 is not None:
            mid = t_traffic0 + 0.5*(t_traffic1 - t_traffic0)
            node['rss_slope_kb_per_min_second_half'] = rss_slope(samples, mid, t_traffic1)
            node['rss_slope_kb_per_min_whole'] = rss_slope(samples, t_traffic0 + skip, t_traffic1)
            inside = [r for t, r, _ in rss if t_traffic0 + skip <= t <= t_traffic1]
            node['rss_kb_traffic_start_end'] = [inside[0], inside[-1]] if inside else None
            # Leak check on the heap in use (glibc mallinfo2 from the node's
            # periodic stats): RSS steps with malloc arena growth and
            # trimming (seen as +/-15-22 MB steps at the same moment on every
            # node), which is not a leak. RSS slope is still reported.
            heap = [(r['mono'], r['heap']['in_use']/1024) for r in stats if r.get('heap')]
            hs = [(t, h) for t, h in heap if mid <= t <= t_traffic1]
            if len(hs) >= 3:
                mx = sum(t for t, _ in hs)/len(hs)
                my = sum(h for _, h in hs)/len(hs)
                sxx = sum((t - mx)**2 for t, _ in hs)
                node['heap_slope_kb_per_min_second_half'] = 60*sum((t - mx)*(h - my) for t, h in hs)/sxx if sxx else None
                node['heap_kb'] = [[round(t - t_launch, 1), round(h)] for t, h in heap]
                sl = node['heap_slope_kb_per_min_second_half']
                checks.append((f'node {n}: heap-in-use slope {sl if sl is None else round(sl, 1)} kB/min over the second half < 100 kB/min',
                               sl is not None and sl < 100))
            else:
                sl = node['rss_slope_kb_per_min_second_half']
                checks.append((f'node {n}: RSS slope {sl if sl is None else round(sl, 1)} kB/min over the second half < 100 kB/min (no heap stats)',
                               sl is not None and sl < 100))
        # Device counters during traffic (before = after warm-up).
        dev, before = res['device'], res['device_before']
        delta = {k: dev[k] - before.get(k, 0) for k in dev}
        node['device_total'] = dev
        node['device_during_traffic'] = delta
        for k in ('tx_late', 'tx_underflow', 'rx_late', 'rx_overflows', 'rx_queue_drops'):
            checks.append((f'node {n}: device {k} during traffic == 0 (total {dev[k]})', delta[k] == 0))
        # Application traffic.
        comp = res.get('compact_received') or {}
        node['received'] = {k: {kk: v for kk, v in r.items() if kk not in ('latency_hist', 'per_second')}
                            for k, r in comp.items()}
        node['per_second'] = {k: r.get('per_second') for k, r in comp.items()}
        node['latency_hist'] = {k: r.get('latency_hist') for k, r in comp.items()}
        for k, r in comp.items():
            checks.append((f'node {n}: flow {k} no duplicates/corrupt ({r["duplicates"]}/{r["corrupt"]})',
                           r['duplicates'] == 0 and r['corrupt'] == 0))
        node['flows'] = res['flows']
        node['rx_counts'] = res.get('rx_counts')
        summary['nodes'][str(n)] = node
    # Production logs: service times and queue depth.
    try:
        for n in results:
            node = summary['nodes'][str(n)]
            logs = read_logs(outdir / 'logs', n)
            if logs is None:
                continue
            send, recv, origin = logs['send'], logs['recv'], logs['origin']
            t0, t1 = node['traffic_window']
            ok = send[(send['dropped'] == 0) & (send['data_len'] > 0)]
            mono = origin + ok['mono_timestamp']
            skip = min(60.0, 0.1*args.duration)
            ok = ok[(mono >= t0 + skip) & (mono <= t1)] if t0 else ok
            # mod: modulation of one packet; llc: network arrival to the
            # controller's pull (queue wait); synth: arrival to modulated.
            node['service'] = {
                'mod_latency_s': percentiles(ok['mod_latency']),
                'llc_latency_s': percentiles(ok['llc_latency']),
                'synth_latency_s': percentiles(ok['synth_latency']),
            }
            if (ok['queue_latency'] > 0).all():   # set only by MandateQueue
                node['service']['queue_latency_s'] = percentiles(ok['queue_latency'])
            node['send_rows'] = int(len(send))
            node['queue_drops'] = int((send['dropped'] != 0).sum())
            if recv is not None and len(recv):
                good = recv[(recv['header_valid'] != 0) & (recv['payload_valid'] != 0)]
                node['service']['demod_latency_s'] = percentiles(good['demod_latency'])
                node['recv_rows'] = int(len(recv))
            if t0:
                node['queue_depth_packets'] = queue_depth(send, origin, t0 + skip, t1)
                limit = queue_bytes/args.size
                qd = node['queue_depth_packets']
                checks.append((f'node {n}: network queue depth p99 {qd["p99"] if qd else None} <= limit {limit:.0f} packets',
                               qd is not None and qd['p99'] <= limit + 1))
    except Exception as e:   # keep the run's other results
        summary['log_error'] = repr(e)
        checks.append(('production logs read', False))
    for n, st in air.items():
        checks.append((f'relay: node {n} bursts late {st["late"]} / backlog drops {st["backlog_drops"]} == 0',
                       st['late'] == 0 and st['backlog_drops'] == 0))
    summary['air_monitor'] = [s for s in monitor if s['node'] == 'air'][-1:] if monitor else None
    summary['exit'] = {str(k): p.returncode for k, p in procs.items()}
    summary['checks'] = [{'check': c, 'passed': bool(ok)} for c, ok in checks]
    summary['passed'] = all(ok for _, ok in checks)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dragonradio', default=str(ROOT / 'dragonradio'))
    parser.add_argument('--outdir', required=True)
    parser.add_argument('--phy', choices=sorted(PHYS), default='native')
    parser.add_argument('--rate', type=float, default=1.0, help='sample rate and bandwidth (MS/s)')
    parser.add_argument('--duration', type=float, default=1800.0, help='seconds of traffic')
    parser.add_argument('--warmup', type=float, default=5.0)
    parser.add_argument('--drain', type=float, default=5.0)
    parser.add_argument('--interval', type=float, default=5.0, help='monitor/stats period (s)')
    parser.add_argument('--size', type=int, default=1000, help='UDP payload bytes')
    parser.add_argument('--offered', type=float, default=70.0,
                        help='offered packets/s per node per MS/s (about 1.5x the one-slot TDMA capacity)')
    parser.add_argument('--queue-per-msps', type=int, default=15000,
                        help='tail-drop queue limit (bytes) per MS/s')
    parser.add_argument('--noise-db', type=float, default=-50.0)
    parser.add_argument('--cores1', required=True)
    parser.add_argument('--cores2', required=True)
    parser.add_argument('--cores-air', required=True)
    parser.add_argument('extra', nargs='*', help='extra radio arguments (after --)')
    args = parser.parse_args()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    rate = args.rate*1e6
    pps = round(args.offered*args.rate, 1)
    queue_bytes = int(args.queue_per_msps*args.rate)
    args.sock = f'/tmp/sim-soak-{os.getpid()}.sock'
    air_cmd = ['taskset', '-c', args.cores_air, sys.executable, str(ROOT / 'scripts' / 'sim-air.py'), args.sock,
               '--stats', str(outdir / 'air.json')]
    (outdir / 'run.json').write_text(json.dumps({
        'argv': sys.argv, 'phy': args.phy, 'rate': rate, 'offered_pps': pps, 'queue_bytes': queue_bytes,
        'started': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'host': os.uname().nodename,
        'loadavg': open('/proc/loadavg').read().strip()}, indent=1))
    air = subprocess.Popen(air_cmd, stdout=open(outdir / 'air.log', 'w'), stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(args.sock):
            break
        time.sleep(0.05)
    t_launch = time.monotonic()
    procs = {}
    for n in (1, 2):
        cmd = node_command(args, n, outdir, rate, pps, queue_bytes)
        (outdir / f'node{n}.cmd').write_text(' '.join(cmd) + '\n')
        procs[n] = subprocess.Popen(cmd, stdout=open(outdir / f'node{n}.log', 'w'), stderr=subprocess.STDOUT,
                                    cwd=str(ROOT))
    deadline = t_launch + args.warmup + args.duration + args.drain + 120
    monitor = []
    with open(outdir / 'monitor.jsonl', 'w') as mon:
        while any(p.poll() is None for p in procs.values()) and time.monotonic() < deadline:
            now = time.monotonic()
            for name, pid in [(1, procs[1].pid), (2, procs[2].pid), ('air', air.pid)]:
                # taskset execs the radio, so the pid is the radio's.
                s = {'mono': now, 'node': name, 'proc': read_proc(pid)}
                monitor.append(s)
                mon.write(json.dumps(s) + '\n')
            mon.flush()
            time.sleep(args.interval)
    for p in procs.values():
        if p.poll() is None:
            p.send_signal(signal.SIGINT)
            try:
                p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    air.send_signal(signal.SIGTERM)
    air.wait(timeout=20)
    summary = summarize(args, outdir, procs, monitor, rate, pps, queue_bytes, t_launch)
    (outdir / 'summary.json').write_text(json.dumps(summary, indent=1))
    for c in summary['checks']:
        print(('  ok    ' if c['passed'] else '  FAIL  ') + c['check'])
    for n, node in summary['nodes'].items():
        rec = ', '.join(f'{k}: {r["unique"]} pkts p50 {1e3*r["latency_p50"]:.0f} ms p99 {1e3*r["latency_p99"]:.0f} ms'
                        for k, r in node['received'].items() if r.get('latency_p50'))
        print(f'  node {n}: headroom {node["headroom"] and round(node["headroom"], 2)} ({node["busiest_radio_thread"]}), '
              f'work CPUs {node["radio_work_cpus"]:.2f} (spinning threads hold {node["spinning_cpus"]:.2f}), RSS HWM {node["rss_hwm_kb"]} kB; {rec}')
    print('PASSED' if summary['passed'] else 'FAILED')
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
