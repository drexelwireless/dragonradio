# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from functools import reduce
import matplotlib as mp
import matplotlib.pyplot as plt
import pandas as pd
import re

from dragonradio.tools.logging.command_line import add_annotate_args, Command

from .plot import pprEvent, pprSentPacket, pprReceivedPacket, pprTXRecord
from .plot import EventPlot, RadioMetricPlot, TrafficPlot

def mkNumericAltPat(ns):
    """Make a regexp that matches one of the given numbers"""
    return reduce(lambda x, y: r'{}|{}'.format(x, y), (re.escape(n) for n in ns))

def canFilterByNode(k):
    return k in ['AMC', 'ARQ', 'TIMESYNC']

def mkNodeFilterPat(nodes):
    """Make a regexp that matches the given nodes"""
    return r'.*node=({})(?!\d)'.format(mkNumericAltPat(nodes))

def canFilterByFlow(k):
    return k in []

def mkFlowFilterPat(flows):
    """Make a regexp that matches the given flows"""
    return r'.*flow=({})(?!\d)'.format(mkNumericAltPat(flows))

def pprSendEvent(row):
    result = f"SEND: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"
    if row.nretrans > 0:
        result += "; retransmitted"
    elif row.dropped == 'll_drop':
        result += f"; dropped by link-layer"
    elif row.dropped == 'queue_drop':
        result += f"; dropped by network queue"
    elif row.dropped == 'phy_drop':
        result += f"; dropped by phy"

    return result

def pprRecvEvent(row):
    return f"RECV: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"

class DumpEventsCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--wall-time', action='store_true',
                            default=False,
                            help='show wall time instead of offset from start')
        parser.add_argument('--events', action='store_true',
                            default=False,
                            help='show events')
        parser.add_argument('--send', action='store_true',
                            default=False,
                            help='show sent packets')
        parser.add_argument('--recv', action='store_true',
                            default=False,
                            help='show received packets')

    def handle(self, parser, args):
        node_ids = sorted(self.logs.nodes)

        for node_id in reversed(node_ids):
            frames = []

            if args.events:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].events.timestamp
                                           , 'event': self.logs[node_id].events.event}))

            if args.send:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].send.timestamp
                                           , 'event': self.logs[node_id].send.apply(pprSendEvent, axis=1)}))

            if args.recv:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].recv.timestamp
                                           , 'event': self.logs[node_id].recv.apply(pprRecvEvent, axis=1)}))

            df = pd.concat(frames)

            if not args.wall_time:
                df.timestamp += self.logs[node_id].delta

            df = df.sort_values(by='timestamp')

            for _, (t, event) in df.iterrows():
                print(f'{t:5.4f}: {event}')

class PlotEventsCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--send', action='store_true',
                            default=False,
                            help='show sent packets')
        parser.add_argument('--recv', action='store_true',
                            default=False,
                            help='show received packets')
        parser.add_argument('--tx-records', action='store_true',
                            default=False,
                            help='show TX records')
        parser.add_argument('--amc', const='AMC', action='append_const',
                            dest='events',
                            help='show AMC events')
        parser.add_argument('--arq', const='ARQ', action='append_const',
                            dest='events',
                            help='show ARQ events')
        parser.add_argument('--phy', const='PHY', action='append_const',
                            dest='events',
                            help='show PHY events')
        parser.add_argument('--queue', const='QUEUE', action='append_const',
                            dest='events',
                            help='show queue events')
        parser.add_argument('--timesync', const='TIMESYNC', action='append_const',
                            dest='events',
                            help='show time synchronization events')
        parser.add_argument('--usrp', const='USRP', action='append_const',
                            dest='events',
                            help='show USRP events')
        parser.add_argument('--mac', const='MAC', action='append_const',
                            dest='events',
                            help='show MAC events')
        parser.add_argument('--tuntap', const='TUNTAP', action='append_const',
                            dest='events',
                            help='show tun/tap events')
        parser.add_argument('--net', const='NET', action='append_const',
                            dest='events',
                            help='show net events')

        parser.add_argument('-n', '--node', action='append',
                            metavar='NODE')
        parser.add_argument('--flow', action='append',
                            metavar='FLOW')

        add_annotate_args(parser)

    def handle(self, parser, args):
        plot = EventPlot(self.logs, annotate=args.annotate)

        node_ids = sorted(self.logs.nodes)

        for node_id in reversed(node_ids):
            if args.events:
                for k in args.events:
                    self.reset_filters()

                    if args.node and canFilterByNode(k):
                        rec = re.compile(mkNodeFilterPat(args.node))

                        self.filter_by(lambda df : df[df.event.str.match(rec)])
                    elif args.flow and canFilterByFlow(k):
                        rec = re.compile(mkFlowFilterPat(args.flow))

                        self.filter_by(lambda df : df[df.event.str.match(rec)])

                    plot.addEventCategory(node_id, k, ppr=pprEvent, filt=self.filter)

            if args.tx_records:
                plot.addEventCategory(node_id, 'TXRECORD', ppr=pprTXRecord)

            if args.send:
                plot.addEventCategory(node_id, 'SEND', ppr=pprSentPacket)

            if args.recv:
                plot.addEventCategory(node_id, 'RECV', ppr=pprReceivedPacket)

        plot.plot()
        plt.show()

class PlotRadioMetricCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--flow', type=int, action='append',
                            dest='flows',
                            metavar='FLOW',
                            help='Restrict to given flow')
        parser.add_argument('--seq', type=int, action='append',
                            dest='seqs',
                            metavar='SEQ',
                            help='Restrict to given sequence number')
        parser.add_argument('--mgen-seqno', type=int, action='append',
                            dest='mgen_seqnos',
                            metavar='SEQ',
                            help='Restrict to given MGEN sequence number')
        parser.add_argument('--no-retrans', action='store_true',
                            default=False,
                            help='ignore retransmitted packets')

        parser.add_argument('--cfo', action='store_const', const='cfo',
                            dest='metric',
                            help='plot CFO')
        parser.add_argument('--evm', action='store_const', const='evm',
                            dest='metric',
                            help='plot EVM')
        parser.add_argument('--rssi', action='store_const', const='rssi',
                            dest='metric',
                            help='plot RSSI')

        parser.add_argument('--mcsidx', action='store_const', const='mcsidx',
                            dest='metric',
                            help='plot MCS index of received packets')
        parser.add_argument('--ms', action='store_const', const='ms',
                            dest='metric',
                            help='plot modulation scheme of received packets')

        parser.add_argument('--interarrival', action='store_const', const='interarrival',
                            dest='metric',
                            help='plot interarrival time')

        parser.add_argument('--demod-latency', action='store_const', const='demod_latency',
                            dest='metric',
                            help='plot demodulation latency')
        parser.add_argument('--rx-latency', action='store_const', const='rx_latency',
                            dest='metric',
                            help='plot packet RX latency')

        parser.add_argument('--sent-mcsidx', action='store_const', const='sent_mcsidx',
                            dest='metric',
                            help='plot MCS index of sent packets')
        parser.add_argument('--sent-ms', action='store_const', const='sent_ms',
                            dest='metric',
                            help='plot modulation scheme of sent packets')

        parser.add_argument('--interdeparture', action='store_const', const='interdeparture',
                            dest='metric',
                            help='plot interdeparture time')

        parser.add_argument('--tuntap-latency', action='store_const', const='tuntap_latency',
                            dest='metric',
                            help='plot tun/tap read latency')
        parser.add_argument('--enqueue-latency', action='store_const', const='enqueue_latency',
                            dest='metric',
                            help='plot packet sender enqueue latency')
        parser.add_argument('--dequeue-latency', action='store_const', const='dequeue_latency',
                            dest='metric',
                            help='plot packet sender dequeue latency')
        parser.add_argument('--queue-latency', action='store_const', const='queue_latency',
                            dest='metric',
                            help='plot packet sender queue latency')
        parser.add_argument('--synth-latency', action='store_const', const='synth_latency',
                            dest='metric',
                            help='plot synthesizer latency')
        parser.add_argument('--llc-latency', action='store_const', const='llc_latency',
                            dest='metric',
                            help='plot LLC latency')
        parser.add_argument('--mod-latency', action='store_const', const='mod_latency',
                            dest='metric',
                            help='plot modulation latency')
        parser.add_argument('--tx-latency', action='store_const', const='tx_latency',
                            dest='metric',
                            help='plot packet TX latency')

        parser.add_argument('--latency', action='store', type=str,
                            dest='latency',
                            metavar='BEGIN,END',
                            help='plot latency between two timestamped times')

        add_annotate_args(parser)

        parser.add_argument('--include-invalid-packets', action='store_true', default=False,
                            help='include invalid packets when displaying metrics')

    def handle(self, parser, args):
        if args.flows:
            self.filter_by(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

        if args.seqs:
            self.filter_by(lambda df: df[df.seq.isin(args.seqs)])

        if args.mgen_seqnos:
            self.filter_by(lambda df: df[df.mgen_seqno.isin(args.mgen_seqnos)])

        if args.no_retrans:
            self.filter_by(lambda df: df[df.nretrans == 0])

        fig = plt.figure()
        ax = fig.add_subplot(1,1,1)

        RadioMetricPlot(fig, ax, self.logs,
                        metric=args.metric,
                        latency=args.latency,
                        nodes=args.srns,
                        filt=self.filter,
                        include_invalid_packets=args.include_invalid_packets,
                        annotate=args.annotate)

        plt.show()

class PlotTrafficCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--traffic', type=str, action='store',
                            help='plot traffic between nodes')
        parser.add_argument('--src', type=int, action='store',
                            help='plot traffic from given source')
        parser.add_argument('--dest', type=int, action='store',
                            help='plot traffic to given destination')
        parser.add_argument('--flow', type=int, action='append',
                            dest='flows',
                            metavar='FLOW',
                            help='Restrict to given flow')
        parser.add_argument('--seq', type=int, action='append',
                            metavar='SEQ',
                            help='Restrict to given sequence number')
        parser.add_argument('--mgen-seq', action='store_const', const='mgen_seqno',
                            default='seq',
                            dest='y',
                            help='plot MGEN sequence number instead of packet sequence number')
        parser.add_argument('--mcsidx', action='store_const', const='mcsidx',
                            dest='y',
                            help='plot MCS index instead of packet sequence number')
        parser.add_argument('--mac-errors', action='store_true',
                            help='show MAC errors')
        parser.add_argument('--sack', action='store_true',
                            help='show selective ACKs')
        parser.add_argument('--by-flow', action='store_true',
                            help='show traffic by flow')

        add_annotate_args(parser)

    def handle(self, parser, args):
        src = None
        dest = None

        if args.traffic:
            nodes = [int(x) for x in args.traffic.split(',')]

            if len(nodes) == 2:
                (src, dest) = nodes
            else:
                src = nodes[0]

        if args.src:
            src = args.src

        if args.dest:
            dest = args.dest

        if args.flows:
            self.filter_by(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

        if args.seq:
            self.filter_by(lambda df: df[df.seq.isin(args.seq)])

        plot = TrafficPlot(plt.figure(), self.logs, src, dest,
                           y=args.y,
                           filt=self.filter,
                           by_flow=args.by_flow,
                           flows=args.flows,
                           mac_errors=args.mac_errors,
                           sack=args.sack,
                           annotate=args.annotate)
        plot.plot()

        plt.show()

def dump_events():
    cmd = DumpEventsCommand()
    cmd.run('Dump DragonRadio events')

def plot_events():
    mp.use('GTK3Agg')

    cmd = PlotEventsCommand()
    cmd.run('Plot DragonRadio events')

def plot_radio_metric():
    mp.use('GTK3Agg')

    cmd = PlotRadioMetricCommand()
    cmd.run('Plot radio metrics')

def plot_traffic():
    mp.use('GTK3Agg')

    cmd = PlotTrafficCommand()
    cmd.run('Plot radio traffic')
