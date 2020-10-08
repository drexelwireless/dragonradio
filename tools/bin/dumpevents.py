#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
import logging

import pandas as pd

from dragonradio.tools.logging import LogCollection

def pprSend(row):
    result = f"SEND: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"
    if row.nretrans > 0:
        result += "; retransmitted"
    elif row.dropped == 'll_drop':
        result += f"; dropped by link-layer"
    elif row.dropped == 'queue_drop':
        result += f"; dropped by network queue"

    return result

def pprRecv(row):
    return f"RECV: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"

def main():
    parser = argparse.ArgumentParser(description='Display DragonRadio event log.')
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

    parser.add_argument('-n', '--node', action='append',
                        metavar='NODE')

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

    # Load logs
    logs = LogCollection(start=start)
    logs.load(args.paths)

    for node_id in args.node:
        node_id = int(node_id)

        frames = []

        if args.events:
            frames.append(pd.DataFrame({ 'timestamp': logs[node_id].events.timestamp
                                       , 'event': logs[node_id].events.event}))

        if args.send:
            frames.append(pd.DataFrame({ 'timestamp': logs[node_id].send.timestamp
                                       , 'event': logs[node_id].send.apply(pprSend, axis=1)}))

        if args.recv:
            frames.append(pd.DataFrame({ 'timestamp': logs[node_id].recv.timestamp
                                       , 'event': logs[node_id].recv.apply(pprRecv, axis=1)}))

        df = pd.concat(frames)

        if not args.wall_time:
            df.timestamp += logs[node_id].delta

        df = df.sort_values(by='timestamp')

        for _, (t, event) in df.iterrows():
            print(f'{t:5.4f}: {event}')

if __name__ == '__main__':
    main()
