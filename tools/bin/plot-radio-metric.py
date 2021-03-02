#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
from functools import reduce
import logging
import pytz

import matplotlib as mp
import matplotlib.pyplot as plt

import dragonradio.tools.colosseum.scoring
import dragonradio.tools.logging
from dragonradio.tools.plot.radio import RadioMetricPlot

mp.use('GTK3Agg')

UTC = pytz.timezone('UTC')

def main():
    parser = argparse.ArgumentParser(description='Show received packets.')
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--start-time', type=float,
                        default=None,
                        metavar='SEC',
                        help='set start time in seconds since the epoch')

    parser.add_argument('-n', '--node', action='append', type=int,
                        dest='nodes',
                        default=None,
                        metavar='NODE',
                        help='include node')
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

    parser.add_argument('paths', nargs='*')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    # Determine start time
    if args.start_time:
        start = datetime.datetime.fromtimestamp(args.start_time, UTC)
    else:
        start = None

    filters = []

    if args.flows:
        filters.append(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

    def compose(f, g):
        return lambda x : f(g(x))

    filt = reduce(compose, filters, lambda x : x)

    # Load logs
    logs = dragonradio.tools.logging.LogCollection(start=start)
    logs.load(args.paths)

    fig = plt.figure()
    ax = fig.add_subplot(1,1,1)

    RadioMetricPlot(fig, ax, logs, args.metric,
                    nodes=args.nodes,
                    filt=filt,
                    include_invalid_packets=args.include_invalid_packets)

    plt.show()

if __name__ == '__main__':
    main()
