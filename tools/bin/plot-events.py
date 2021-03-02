#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
from functools import reduce
import logging
import pytz
import re

import numpy as np

import matplotlib as mp
import matplotlib.pyplot as plt

import dragonradio.tools.colosseum.scoring
from dragonradio.tools.plot.radio import pprEvent, pprSentPacket, pprReceivedPacket
from dragonradio.tools.plot.radio import EventPlot

mp.use('GTK3Agg')

UTC = pytz.timezone('UTC')

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

def main():
    parser = argparse.ArgumentParser(description='Plot DragonRadio events.')
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
    parser.add_argument('--srn', type=int, action='append',
                        dest='srns',
                        metavar='NODE')

    parser.add_argument('--send', action='store_true',
                        default=False,
                        help='show sent packets')
    parser.add_argument('--recv', action='store_true',
                        default=False,
                        help='show received packets')
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

    parser.add_argument('-n', '--node', action='append',
                        metavar='NODE')
    parser.add_argument('--flow', action='append',
                        metavar='FLOW')

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
    logs = dragonradio.tools.logging.LogCollection(start=start)
    logs.load(args.paths, srns=args.srns)

    plot = EventPlot(logs)

    node_ids = sorted(logs.nodes)

    for node_id in reversed(node_ids):
        if args.events:
            for k in args.events:
                if args.node and canFilterByNode(k):
                    rec = re.compile(mkNodeFilterPat(args.node))

                    filt = lambda df : df[df.event.str.match(rec)]
                elif args.flow and canFilterByFlow(k):
                    rec = re.compile(mkFlowFilterPat(args.flow))

                    filt = lambda df : df[df.event.str.match(rec)]
                else:
                    filt = None

                plot.addEventCategory(node_id, k, ppr=pprEvent, filt=filt)

        if args.send:
            plot.addEventCategory(node_id, 'SEND', ppr=pprSentPacket)

        if args.recv:
            plot.addEventCategory(node_id, 'RECV', ppr=pprReceivedPacket)

    plot.plot()
    plt.show()

if __name__ == '__main__':
    main()
