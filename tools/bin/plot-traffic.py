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
from dragonradio.tools.logging import LogCollection
from dragonradio.tools.plot.radio import TrafficPlot

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
    parser.add_argument('--scenarios',
                        type=str,
                        default=None,
                        metavar='DIR',
                        help='specify directory for scenario files')
    parser.add_argument('--srn-logs', type=str,
                        default=None,
                        metavar='DIR',
                        help='directory where node logs are located')

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

    # Set path to scenarios
    if args.scenarios is not None:
        dragonradio.tools.colosseum.scoring.scenarios_path = args.scenarios

    # Load logs
    logs = LogCollection(start=start)
    logs.load(args.paths)

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

    filters = []

    if args.flows:
        filters.append(lambda df: df[df.mgen_flow_uid.isin(args.flows)])

    if args.seq:
        filters.append(lambda df: df[df.seq.isin(args.seq)])

    def compose(f, g):
        return lambda x : f(g(x))

    filt = reduce(compose, filters, lambda x : x)

    plot = TrafficPlot(plt.figure(), logs, src, dest,
                       y=args.y,
                       filt=filt,
                       mac_errors=args.mac_errors)
    plot.plot()

    plt.show()

if __name__ == '__main__':
    main()
