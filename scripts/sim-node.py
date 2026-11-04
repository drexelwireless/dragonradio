# One node of a full-stack simulation (phase P2).
#
# Run inside the radio executable, one process per node, alongside
# scripts/sim-air.py:
#
#   dragonradio scripts/sim-node.py -i 1 -n 2 --sim-air /tmp/air.sock \
#       --sim-traffic --flow 2:5001:200:500 --duration 20 --results n1.json
#
# The production radio is this branch's dragonradio.controller.Radio, built
# as the class course's sdr-class-radio.py builds it (Config, Radio, TDMA
# schedule from -n, radio.start()): PHY, channelizer, synthesizer, controller,
# queues and MAC are the production ones; only the device (SimUSRP) is
# simulated. With --sim-traffic, SimTraffic also stands in for the tap
# interface; without it, the radio opens a real tap device, normally in a
# private network namespace (unshare -rn) where scripts/sim-netapp.py runs.
#
# Ported from codex/ofdma-simulation (where it drove dragonradio.radio.Radio).
import asyncio
import ctypes
import json
import logging
import sys
import time

import numpy as np

from dragonradio.config import Config
from dragonradio.controller import Radio
from dragonradiocpp.radio import RxRecorder, SimUSRP  # pylint: disable=no-name-in-module
from dragonradiocpp.radio import clock, monoNow, wallNow  # pylint: disable=no-name-in-module
from dragonradiocpp.radio import Mandate, MandateMap, MandateQueue  # pylint: disable=no-name-in-module

logger = logging.getLogger('sim-node')


def parse_flow(s):
    """DEST:PORT:RATE:SIZE[:COUNT] (DEST 255 = broadcast)"""
    parts = s.split(':')
    if len(parts) not in (4, 5):
        raise ValueError(f'bad flow {s!r}; expected DEST:PORT:RATE:SIZE[:COUNT]')
    count = int(parts[4]) if len(parts) == 5 else 0
    return int(parts[0]), int(parts[1]), float(parts[2]), int(parts[3]), count


def parse_link(s):
    """SRC:GAIN_DB[:CFO_HZ[:DELAY[:ERASURE[:JITTER[:SFO_PPM]]]]], the link from SRC to this node"""
    parts = s.split(':')
    if not 2 <= len(parts) <= 7:
        raise ValueError(f'bad link {s!r}; expected SRC:GAIN_DB[:CFO_HZ[:DELAY[:ERASURE[:JITTER[:SFO_PPM]]]]]')
    vals = [int(parts[0]), float(parts[1]), 0.0, 0, 0.0, 0, 0.0]
    for i, p in enumerate(parts[2:], start=2):
        vals[i] = int(p) if i in (3, 5) else float(p)
    return tuple(vals)


def parse_link_ppm(s):
    """SRC:PPM, one oscillator offset of SRC relative to this node: carrier
    offset PPM * 1e-6 * frequency and sample clock offset PPM (a USRP derives
    its LO and ADC clocks from the same reference)"""
    src, sep, ppm = s.partition(':')
    if not sep:
        raise ValueError(f'bad --link-ppm {s!r}; expected SRC:PPM')
    return int(src), float(ppm)


def parse_mandate(s):
    """PORT:MAX_LATENCY_S, a latency mandate for the flow to UDP/TCP port PORT"""
    port, sep, latency = s.partition(':')
    if not sep:
        raise ValueError(f'bad mandate {s!r}; expected PORT:MAX_LATENCY_S')
    return int(port), float(latency)


def parse_schedule(s):
    """MONO:N,N,... install a one-channel TDMA schedule (node per slot) at host
    CLOCK_MONOTONIC time MONO"""
    when, sep, slots = s.partition(':')
    if not sep:
        raise ValueError(f'bad schedule {s!r}; expected MONO:NODE,NODE,...')
    return float(when), [int(n) for n in slots.split(',')]


def parse_set(s):
    """KEY=VALUE: set a Config attribute that has no command-line option"""
    key, sep, value = s.partition('=')
    if not sep:
        raise ValueError(f'bad --set {s!r}; expected KEY=VALUE')
    if value == 'None':
        return key, None
    for conv in (int, float):
        try:
            return key, conv(value)
        except ValueError:
            pass
    return key, value


class _MallInfo2(ctypes.Structure):
    _fields_ = [(name, ctypes.c_size_t) for name in
                ('arena', 'ordblks', 'smblks', 'hblks', 'hblkhd', 'usmblks', 'fsmblks', 'uordblks', 'fordblks', 'keepcost')]


def heap_in_use():
    """Bytes allocated by malloc and not freed (glibc mallinfo2): the leak
    check's measure, independent of arena growth and trimming in RSS."""
    try:
        libc = ctypes.CDLL(None)
        libc.mallinfo2.restype = _MallInfo2
        m = libc.mallinfo2()
        return {'uordblks': m.uordblks, 'hblkhd': m.hblkhd, 'arena': m.arena, 'fordblks': m.fordblks,
                'in_use': m.uordblks + m.hblkhd}
    except (AttributeError, OSError):
        return None


def hist_percentile(hist, q, bins):
    """Upper edge of the latency-histogram bin holding quantile q."""
    lo, ratio, _n = bins
    total = sum(hist)
    if not total:
        return None
    target = q*total
    acc = 0
    for i, c in enumerate(hist):
        acc += c
        if acc >= target:
            return lo*ratio**(i + 1)
    return lo*ratio**len(hist)


def traffic_summary(traffic, bins):
    """Bounded per-flow receive statistics (compact or detailed mode)."""
    out = {}
    for (src, port), st in traffic.received.items():
        lat = st['latency']
        e = {'src': src, 'port': port, 'unique': st['unique'], 'duplicates': st['duplicates'],
             'corrupt': st['corrupt'], 'out_of_order': st['out_of_order'], 'stale': st['stale'],
             'bytes': st['bytes'], 'first_rx': st['first_rx'], 'last_rx': st['last_rx']}
        if lat:
            e.update(latency_mean=sum(lat)/len(lat), latency_p50=percentile(lat, 0.5),
                     latency_p95=percentile(lat, 0.95), latency_p99=percentile(lat, 0.99),
                     latency_p999=percentile(lat, 0.999), latency_max=max(lat))
        elif st['unique']:
            h = st['latency_hist']
            e.update(latency_mean=st['latency_sum']/st['unique'], latency_min=st['latency_min'],
                     latency_max=st['latency_max'],
                     latency_p50=hist_percentile(h, 0.5, bins), latency_p95=hist_percentile(h, 0.95, bins),
                     latency_p99=hist_percentile(h, 0.99, bins), latency_p999=hist_percentile(h, 0.999, bins),
                     latency_hist={str(i): c for i, c in enumerate(h) if c}, per_second=st['per_second'])
        out[f'{src}:{port}'] = e
    return out


def percentile(xs, q):
    if not xs:
        return None
    xs = sorted(xs)
    k = min(len(xs) - 1, max(0, int(round(q*(len(xs) - 1)))))
    return xs[k]


async def experiment(radio, config):
    try:
        # With --sim-traffic the radio generates traffic itself; otherwise a
        # real tap interface carries whatever the node's applications send
        # (scripts/sim-netapp.py in the node's network namespace).
        traffic = radio.tuntap if config.sim_traffic else None
        links = {l[0]: list(l) for l in config.links}
        for (src, ppm) in config.link_ppm:
            l = links.setdefault(src, [src, 0.0, 0.0, 0, 0.0, 0, 0.0])
            l[2] += ppm*1e-6*radio.frequency
            l[6] += ppm
        for (src, gain_db, cfo_hz, delay, erasure, jitter, sfo_ppm) in links.values():
            radio.usrp.setLink(src, gain_db, cfo_hz, delay, erasure, jitter, sfo_ppm)
        applied_links = [radio.usrp.getLink(src) | {'src': src} for src in links]

        # Latency mandates, installed as the collaboration controller does:
        # flow performance tracking always, the mandate queue if selected
        # (it sets each packet's deadline from max_latency_s).
        if config.mandates:
            mandates = MandateMap()
            for (port, latency) in config.mandates:
                mandates[port] = Mandate(port, 1.0, 1, latency, None, None)
            radio.flowperf.mandates = mandates
            if isinstance(radio.netq, MandateQueue):
                radio.netq.mandates = mandates

        # Timed MAC schedule changes (grants) through Radio.installMACSchedule.
        schedule_log = []

        async def apply_schedule(when, slots):
            await asyncio.sleep(max(0.0, when - time.monotonic()))
            radio.installMACSchedule(np.array([slots]))
            schedule_log.append({'requested': when, 'applied': time.monotonic(), 'slots': slots})
        for (when, slots) in config.schedules:
            radio.createTask(apply_schedule(when, slots), name=f'schedule at {when}')
        if config.flows and traffic is None:
            raise ValueError('--flow requires --sim-traffic')
        for (dest, port, rate, size, count) in config.flows:
            traffic.addFlow(port, dest, rate, size, count, 0.0)

        # Time synchronization (radio/timesync.py) without the collaboration
        # controller: the time master is a gateway, every node broadcasts
        # timestamped HELLOs, and others periodically fit their WallClock to
        # the master's. Clock samples pair the shared host clock with this
        # node's device and wall clocks.
        if config.time_master is not None and config.time_master == radio.node_id:
            radio.nhood.me.is_gateway = True
        clock_log = []

        def clock_sample():
            return {'host': time.monotonic(), 'mono': monoNow(), 'wall': wallNow(),
                    'offset': clock.offset.total_seconds(), 'skew': clock.skew}

        async def hello_task():
            while True:
                radio.controller.broadcastHello()
                await asyncio.sleep(config.hello_period)

        async def timesync_task():
            while True:
                await asyncio.sleep(config.timesync_period)
                try:
                    radio.synchronizeClock()
                except Exception:  # pylint: disable=broad-except
                    logger.exception('timesync failed')
                clock_log.append(dict(clock_sample(), event='sync'))

        async def clock_task():
            while True:
                clock_log.append(clock_sample())
                await asyncio.sleep(1.0)
        if config.hello_period:
            radio.createTask(hello_task(), name='hello')
        if config.timesync_period:
            radio.createTask(timesync_task(), name='timesync')
        radio.createTask(clock_task(), name='clock samples')

        def device_stats():
            # Simulated device: full counters. UHD device (--hardware): the TX
            # late/underflow counters reset on read, so a read at traffic
            # start and end gives the counts during traffic; RX errors are
            # only logged and are counted from the log by the OTA node script.
            if isinstance(radio.usrp, SimUSRP):
                return radio.usrp.stats
            return {'tx_late': radio.usrp.tx_late_count, 'tx_underflow': radio.usrp.tx_underflow_count,
                    'rx_late': 0, 'rx_overflows': 0, 'rx_queue_drops': 0, 'hardware': True}

        if config.ready_file:
            with open(config.ready_file, 'w') as f:
                json.dump({'node': radio.node_id, 'mono': time.monotonic(), 'wall': time.time()}, f)
        await asyncio.sleep(config.warmup)
        usrp_before = device_stats()
        t_start = time.monotonic()
        if traffic:
            traffic.start()

        # Periodic counters for long runs: device, traffic and PHY totals.
        async def periodic_stats():
            bins = type(traffic).latency_bins if traffic else None
            with open(config.stats_file, 'a') as f:
                while True:
                    rec = {'mono': time.monotonic(), 'heap': heap_in_use(),
                           'device': radio.usrp.stats if isinstance(radio.usrp, SimUSRP) else None,
                           'rx_counts': {str(k): v for k, v in radio.rx_recorder.counts.items()}}
                    if traffic:
                        rec['flows'] = [{'port': fl['port'], 'dest': fl['dest'], 'sent': fl['sent']} for fl in traffic.flows]
                        rec['received'] = {k: {kk: v[kk] for kk in ('unique', 'duplicates', 'corrupt', 'stale', 'bytes',
                                                                    'latency_p50', 'latency_p99', 'latency_max') if kk in v}
                                           for k, v in traffic_summary(traffic, bins).items()}
                    f.write(json.dumps(rec) + '\n')
                    f.flush()
                    await asyncio.sleep(config.stats_interval)
        stats_task = asyncio.create_task(periodic_stats()) if config.stats_file else None
        await asyncio.sleep(config.duration)
        if traffic:
            traffic.stop()
        t_stop = time.monotonic()
        await asyncio.sleep(config.drain)
        if stats_task:
            stats_task.cancel()

        # Host monotonic time of this snapshot (the simulation clock): frames
        # a peer sends after it cannot appear in rx_frames.
        snapshot_mono = time.monotonic()
        rx_frames = radio.rx_recorder.records
        received = {}
        for (src, port), st in (traffic.received.items() if traffic and traffic.detail else []):
            lat = st['latency']
            received[f'{src}:{port}'] = {
                'src': src, 'port': port,
                'unique': st['unique'], 'duplicates': st['duplicates'],
                'corrupt': st['corrupt'], 'out_of_order': st['out_of_order'],
                'bytes': st['bytes'], 'seqs': st['seqs'], 'dup_seqs': st['dup_seqs'],
                'first_rx': st['first_rx'], 'last_rx': st['last_rx'], 'arrivals': st['arrivals'],
                'latency_mean': sum(lat)/len(lat) if lat else None,
                'latency_p50': percentile(lat, 0.5),
                'latency_p95': percentile(lat, 0.95),
                'latency_max': max(lat) if lat else None,
            }
        result = {
            'node_id': radio.node_id,
            'phy': config.phy, 'mac': config.mac, 'arq': config.arq,
            'bandwidth': config.bandwidth,
            'rx_rate': radio.usrp.rx_rate, 'tx_rate': radio.usrp.tx_rate,
            'traffic_seconds': t_stop - t_start,
            'tap': traffic is None,
            'flows': traffic.flows if traffic else [],
            'received': received,
            'foreign_frames': traffic.foreign if traffic else None,
            'device_before': {k: 0 for k in usrp_before} if config.hardware else usrp_before,
            'device': ({k: v + usrp_before.get(k, 0) if isinstance(v, int) else v for k, v in device_stats().items()}
                       if config.hardware else device_stats()),
            'hardware': config.hardware,
            # Every frame the PHY handed up: (timestamp, curhop, nexthop, seq,
            # flags, header_valid, payload_valid, data_len, mcsidx, evm, rssi, cfo)
            'rx_frames': rx_frames,
            'rx_counts': {str(k): v for k, v in radio.rx_recorder.counts.items()},
            'soak': config.soak,
            'compact_received': (traffic_summary(traffic, type(traffic).latency_bins)
                                 if traffic and not traffic.detail else None),
            'snapshot_mono': snapshot_mono,
            'links': [list(l) for l in config.links],
            'link_ppm': [list(l) for l in config.link_ppm],
            'applied_links': applied_links,
            'frequency': radio.frequency,
            'impairments': ({k: [v.real, v.imag] if isinstance(v, complex) else v
                             for k, v in radio.usrp.impairments.items()}
                            if isinstance(radio.usrp, SimUSRP) else None),
            'mandates': [list(m) for m in config.mandates],
            'schedules': schedule_log,
            # map: (host, device) time pairs of the simulated device clock, so
            # the harness maps device times (send logs, frame timestamps) to
            # host time exactly (the clock's rate error counts from its epoch).
            'clock': {'sim_offset': config.sim_clock_offset, 'sim_ppm': config.sim_clock_ppm,
                      'map': ([[h, radio.usrp.hostToDevice(h)] for h in (snapshot_mono - 100.0, snapshot_mono)]
                              if isinstance(radio.usrp, SimUSRP) else None),
                      'time_master': config.time_master, 'samples': clock_log},
            'queue': config.queue,
        }
        with open(config.results, 'w') as f:
            json.dump(result, f, indent=1)
        logger.info('wrote %s', config.results)
    except Exception:
        logger.exception('experiment failed')
    finally:
        radio.stop()


def main():
    config = Config()
    config.mac = 'tdma'
    config.num_nodes = 2

    parser = config.parser()
    exp = parser.add_argument_group('Full-stack experiment')
    exp.add_argument('--flow', action='append', type=parse_flow, dest='flows', default=[],
                     metavar='DEST:PORT:RATE:SIZE[:COUNT]', help='add a UDP flow from this node')
    exp.add_argument('--link', action='append', type=parse_link, dest='links', default=[],
                     metavar='SRC:GAIN_DB[:CFO_HZ[:DELAY[:ERASURE[:JITTER[:SFO_PPM]]]]]', help='set the link from SRC to this node')
    exp.add_argument('--link-ppm', action='append', type=parse_link_ppm, dest='link_ppm', default=[],
                     metavar='SRC:PPM', help='oscillator offset of SRC: carrier and sample clock (added to --link)')
    exp.add_argument('--mandate', action='append', type=parse_mandate, dest='mandates', default=[],
                     metavar='PORT:MAX_LATENCY_S', help='latency mandate for the flow to PORT')
    exp.add_argument('--schedule-at', action='append', type=parse_schedule, dest='schedules', default=[],
                     metavar='MONO:NODE,NODE,...', help='install a TDMA schedule at a host monotonic time')
    exp.add_argument('--warmup', type=float, default=3.0, help='seconds before traffic starts')
    exp.add_argument('--duration', type=float, default=10.0, help='seconds of traffic')
    exp.add_argument('--drain', type=float, default=3.0, help='seconds after traffic stops')
    exp.add_argument('--results', required=True, help='JSON results path')
    exp.add_argument('--hardware', action='store_true',
                     help='run on a real USRP (--addr) instead of --sim-air; used only by scripts/ota-fullstack.py --execute')
    exp.add_argument('--ready-file', help='write this file once the radio is running (readiness barrier)')
    exp.add_argument('--time-master', type=int, help='node that is the time master (a gateway)')
    exp.add_argument('--hello-period', type=float, help='broadcast a timestamped HELLO every this many seconds')
    exp.add_argument('--timesync-period', type=float,
                     help='fit WallClock to the time master every this many seconds (radio.synchronizeClock)')
    exp.add_argument('--soak', action='store_true',
                     help='long run: bounded traffic statistics and PHY counts instead of per-packet records')
    exp.add_argument('--stats-file', help='append periodic device/traffic/PHY counters (JSON lines) here')
    exp.add_argument('--stats-interval', type=float, default=10.0, help='seconds between --stats-file records')
    exp.add_argument('--set', action='append', type=parse_set, dest='config_overrides', default=[],
                     metavar='KEY=VALUE', help='set a configuration attribute (e.g. header_fec1=none)')

    try:
        parser.parse_args(namespace=config)
    except SystemExit as err:
        return err.code

    if config.sim_air is None and not config.hardware:
        parser.error('sim-node requires --sim-air (or --hardware, which opens a real radio)')
    if config.sim_air is not None and config.hardware:
        parser.error('--hardware and --sim-air are exclusive')

    for key, value in config.config_overrides:
        if not hasattr(config, key):
            parser.error(f'--set: unknown configuration attribute {key}')
        setattr(config, key, value)

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=config.loglevel)

    if config.log_directory:
        config.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events', 'log_arq_events']

    radio = Radio(config, config.mac)

    # Record every demodulated frame between the channelizer and the
    # controller; packets pass through unchanged.
    radio.rx_recorder = RxRecorder()
    if config.soak:
        radio.rx_recorder.keep_records = False
        if config.sim_traffic:
            radio.tuntap.detail = False
    radio.channelizer.source.disconnect()
    radio.channelizer.source >> radio.rx_recorder.input  # pylint: disable=pointless-statement
    radio.rx_recorder.output >> radio.controller.radio_in  # pylint: disable=pointless-statement

    radio.createTask(experiment(radio, config), name='experiment')
    return radio.start()


if __name__ == '__main__':
    sys.exit(main())
