#!/usr/bin/env python3
import argparse
import logging
import matplotlib as mp
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import re
import sys
import time

import drlog

def main():
    parser = argparse.ArgumentParser(description='Display DragonRadio event log.')
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
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
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s', level=args.loglevel)

    for path in args.paths:
        try:
            log = drlog.Log(recv=args.recv, send=args.send)
            node = log.load(path)

            if args.wall_time:
                offset = node.log_attrs['start']
            else:
                offset = 0

            if args.events:
                print('# timestamp event')
                events = log.events[node.node_id]
                for index, row in events.iterrows():
                    print('{:5.4f}: {}'.format(row[0] + offset, row[1]))

            if args.recv:
                print('# timestamp curhop nexthop seq ms')
                recv = log.received[node.node_id]
                for index, row in recv.iterrows():
                    print('{:5.4f}: {} {} {} {}'.format(row.timestamp + offset, row.curhop, row.nexthop, row.seq, row.ms))

            if args.send:
                print('# timestamp curhop nexthop seq')
                send = log.sent[node.node_id]
                for index, row in send.iterrows():
                    print('{:5.4f}: {} {} {}'.format(row.timestamp + offset, row.curhop, row.nexthop, row.seq))
        except:
            logging.exception("Could not load '%s'", path)

if __name__ == '__main__':
    main()
