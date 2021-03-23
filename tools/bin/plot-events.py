#!/usr/bin/env python3
# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from functools import reduce
import re

import matplotlib as mp
import matplotlib.pyplot as plt

from dragonradio.tools.logging.command_line import Command
from dragonradio.tools.plot.radio import pprEvent, pprSentPacket, pprReceivedPacket, pprTXRecord
from dragonradio.tools.plot.radio import EventPlot

mp.use('GTK3Agg')

def mkNumericAltPat(ns):
    """Make a regexp that matches one of the given numbers"""
    if len(ns) == 1:
        return str(re.escape(ns[0]))
    else:
        return reduce(lambda x, y: r'{}|{}'.format(x, re.escape(y)), ns)

def canFilterByNode(k):
    return k in ['AMC', 'ARQ', 'TIMESYNC']

def mkNodeFilterPat(nodes):
    """Make a regexp that matches the given nodes"""
    return r'.*node=({})[^\d]'.format(mkNumericAltPat(nodes))

def canFilterByFlow(k):
    return k in []

def mkFlowFilterPat(flows):
    """Make a regexp that matches the given flows"""
    return r'.*flow=({})[^\d]'.format(mkNumericAltPat(flows))

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

        parser.add_argument('-n', '--node', action='append',
                            metavar='NODE')
        parser.add_argument('--flow', action='append',
                            metavar='FLOW')

    def handle(self, parser, args):
        plot = EventPlot(self.logs)

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

if __name__ == '__main__':
    cmd = PlotEventsCommand()
    cmd.run('Plot DragonRadio events')
