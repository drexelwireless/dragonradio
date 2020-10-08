#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import logging
import matplotlib.pyplot as plt

from dragonradio.tools.colosseum import ReservationLog
from dragonradio.tools.plot.mgen import Metric, TrafficMetricPlot

def main():
    parser = argparse.ArgumentParser(description='Plot an MGEN traffic metric.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('-m', '--metric', action='store', type=Metric,
                        choices=list(Metric),
                        default='rate',
                        dest='metric',
                        help='set metric')
    parser.add_argument('-w', '--window', action='store', type=int, default=10,
                        dest='window',
                        metavar='SEC',
                        help='set window size')
    parser.add_argument('-t', '--team', action='store', type=int, default=1,
                        dest='team',
                        help='set team')
    parser.add_argument('-a', '--aggregate', action='store_const', const=False,
                        default=False,
                        dest='per_flow',
                        help='show aggregate metric')
    parser.add_argument('--per-flow', action='store_const', const=True,
                        dest='per_flow',
                        help='show per-flow metric')
    parser.add_argument('--flow', action='append', type=int,
                        dest='flow',
                        metavar='FLOW_UID',
                        help='restrict to flow')
    parser.add_argument('--src', action='store', type=int,
                        dest='src',
                        metavar='NODEID',
                        help='restrict to traffic FROM scenario node')
    parser.add_argument('--dest', action='store', type=int,
                        dest='dest',
                        metavar='NODEID',
                        help='restrict to traffic TO scenario node')
    parser.add_argument('--srn-src', action='store', type=int,
                        dest='srn_src',
                        metavar='NODEID',
                        help='restrict to traffic FROM SRN')
    parser.add_argument('--srn-dest', action='store', type=int,
                        dest='srn_dest',
                        metavar='NODEID',
                        help='restrict to traffic TO SRN')
    parser.add_argument('dir')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    reservation = ReservationLog(args.dir, cache_path='cache')

    title = 'Reservation {}'.format(reservation.reservation_id)

    df = reservation.traffic.reset_index()
    df.set_index(['team', 'traffic_src', 'traffic_dest', 'flow_uid', 'send_time'],
                 inplace=True)
    df.sort_index(inplace=True)
    df.reset_index(inplace=True)

    if args.team:
        df = df[df.team == args.team]

    if args.src:
        df = df[df.traffic_src == args.src]

    if args.dest:
        df = df[df.traffic_dest == args.dest]

    if args.srn_src:
        df = df[df.srn_src == args.srn_src]

    if args.srn_dest:
        df = df[df.srn_dest == args.srn_dest]

    if args.flow:
        df = df[df.flow_uid.isin(args.flow)]

    fig = plt.figure(num=title)
    ax = fig.add_subplot(1,1,1)

    TrafficMetricPlot(fig, ax, reservation, df, title,
                      args.metric,
                      args.window,
                      per_flow=args.per_flow)

    plt.show()

if __name__ == '__main__':
    main()
