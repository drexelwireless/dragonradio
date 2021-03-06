#!/usr/bin/env python3
# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import matplotlib as mp
import matplotlib.pyplot as plt

from dragonradio.tools.logging.command_line import Command
from dragonradio.tools.plot.radio import RadioMetricPlot

mp.use('GTK3Agg')

class RadioMetricCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--flow', type=int, action='append',
                            dest='flows',
                            metavar='FLOW',
                            help='Restrict to given flow')

        parser.add_argument('--cfo', action='store_const', const='cfo',
                            dest='metric',
                            help='plot CFO')
        parser.add_argument('--demod-latency', action='store_const', const='demod_latency',
                            dest='metric',
                            help='plot demodulation latency')
        parser.add_argument('--evm', action='store_const', const='evm',
                            dest='metric',
                            help='plot EVM')
        parser.add_argument('--mcsidx', action='store_const', const='mcsidx',
                            dest='metric',
                            help='plot MCS index of received packets')
        parser.add_argument('--mod-latency', action='store_const', const='mod_latency',
                            dest='metric',
                            help='plot modulation latency')
        parser.add_argument('--ms', action='store_const', const='ms',
                            dest='metric',
                            help='plot modulation scheme of received packets')
        parser.add_argument('--rssi', action='store_const', const='rssi',
                            dest='metric',
                            help='plot RSSI')
        parser.add_argument('--sent-mcsidx', action='store_const', const='sent_mcsidx',
                            dest='metric',
                            help='plot MCS index of sent packets')
        parser.add_argument('--sent-ms', action='store_const', const='sent_ms',
                            dest='metric',
                            help='plot modulation scheme of sent packets')

        parser.add_argument('--include-invalid-packets', action='store_true', default=False,
                            help='include invalid packets when displaying metrics')

    def handle(self, parser, args):
        if args.flows:
            self.filter_by(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

        fig = plt.figure()
        ax = fig.add_subplot(1,1,1)

        RadioMetricPlot(fig, ax, self.logs, args.metric,
                        nodes=args.srns,
                        filt=self.filter,
                        include_invalid_packets=args.include_invalid_packets)

        plt.show()

if __name__ == '__main__':
    cmd = RadioMetricCommand()
    cmd.run('Plot radio metrics')
