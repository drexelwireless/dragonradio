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
        parser.add_argument('--seq', type=int, action='append',
                            dest='seqs',
                            metavar='SEQ',
                            help='Restrict to given sequence number')

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
                            help='plot latency between two timestamped times')

        parser.add_argument('--include-invalid-packets', action='store_true', default=False,
                            help='include invalid packets when displaying metrics')

    def handle(self, parser, args):
        if args.flows:
            self.filter_by(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

        if args.seqs:
            self.filter_by(lambda df: df[df.seq.isin(args.seqs)])

        fig = plt.figure()
        ax = fig.add_subplot(1,1,1)

        RadioMetricPlot(fig, ax, self.logs,
                        metric=args.metric,
                        latency=args.latency,
                        nodes=args.srns,
                        filt=self.filter,
                        include_invalid_packets=args.include_invalid_packets)

        plt.show()

if __name__ == '__main__':
    cmd = RadioMetricCommand()
    cmd.run('Plot radio metrics')
