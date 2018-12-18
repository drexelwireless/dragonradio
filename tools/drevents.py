#!/usr/bin/env python3
import argparse
from functools import reduce
import logging
import re

from drlog import EventLog

def main():
    parser = argparse.ArgumentParser(description='Display DragonRadio events.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('--send', action='store_true',
                        default=False,
                        help='show sent packets')
    parser.add_argument('--recv', action='store_true',
                        default=False,
                        help='show received packets')
    parser.add_argument('--phy', action='store_true',
                        default=False,
                        help='show PHY events')
    parser.add_argument('--amc', action='store_true',
                        default=False,
                        help='show AMC events')
    parser.add_argument('--arq', action='store_true',
                        default=False,
                        help='show ARQ events')
    parser.add_argument('--timesync', action='store_true',
                        default=False,
                        help='show time snchronization events')
    parser.add_argument('--usrp', action='store_true',
                        default=False,
                        help='show USRP error events')
    parser.add_argument('-n', '--node', action='append',
                        metavar='NODE')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    e = EventLog(send=args.send, recv=args.recv)

    if args.node:
        if len(args.node) == 1:
            re_nodes = str(args.node[0])
        else:
            re_nodes = reduce(lambda x, y: r'{}|{}'.format(x, y), args.node)

        r_filter = re.compile('.*node=({})[^\d]'.format(re_nodes))
    else:
        r_filter = None

    for path in args.paths:
        try:
            e.loadLog(path)
        except:
            logging.exception("Could not load '%s'", path)

    e.parse(send=args.send, recv=args.recv, r_filter=r_filter)

    node_ids = sorted(e.log.nodes)

    for node_id in reversed(node_ids):
        node = e.log.nodes[node_id]

        if args.phy:
            e.addSeriesCategory(node, 'PHY')

        if args.arq:
            e.addSeriesCategory(node, 'ARQ')

        if args.amc:
            e.addSeriesCategory(node, 'AMC')

        if args.timesync:
            e.addSeriesCategory(node, 'TIMESYNC')

        if args.usrp:
            e.addSeriesCategory(node, 'USRP')

        if args.send:
            e.addSeriesCategory(node, 'sent')

        if args.recv:
            e.addSeriesCategory(node, 'recv')

    e.plot()

if __name__ == '__main__':
    main()
