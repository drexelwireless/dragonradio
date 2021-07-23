#!/usr/bin/env python3
# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import matplotlib as mp
import matplotlib.pyplot as plt

from dragonradio.tools.logging.command_line import Command
from dragonradio.tools.plot.radio import TrafficPlot

mp.use('GTK3Agg')

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

        parser.add_argument('--annotate', action='store_const', const=True,
                            dest='annotate',
                            default=False,
                            help='show annotations')
        parser.add_argument('--no-annotate', action='store_const', const=False,
                            dest='annotate',
                            help='do not show annotations')

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
                           mac_errors=args.mac_errors,
                           sack=args.sack,
                           annotate=args.annotate)
        plot.plot()

        plt.show()

if __name__ == '__main__':
    cmd = PlotTrafficCommand()
    cmd.run('Plot Colosseum traffic')
