#!/usr/bin/env python3
import argparse
import logging
import matplotlib as mp
from matplotlib.text import OffsetFrom
from matplotlib.widgets import Button, Slider
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import sys
import time

import drlog
import drgui

def main():
    parser = argparse.ArgumentParser(description='Dump DragonRadio log info.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('-o', '--output', action='store',
                        help='output file')
    parser.add_argument('-n', '--node', action='store', type=int,
                        dest='node_id',
                        help='specify node')
    parser.add_argument('--header', action='store_true',
                        help='show log header')
    parser.add_argument('--received', action='store_true',
                        help='show received packets')
    parser.add_argument('--events', action='store_true',
                        help='show events')
    parser.add_argument('--bad', action='store_true',
                        help='list bad packets')
    parser.add_argument('--dump-slot', action='store', type=float,
                        help='dump samples from given slot')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    log = drlog.Log()

    for path in args.paths:
        try:
            log.load(path)
        except:
            logging.exception("Could not load '%s'", path)

    if args.header:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            print("Node {}:".format(node_id))
            print("\t{}".format(time.strftime('%Y-%m-%d - %H:%m:%S %p', time.localtime(node.start))))
            for attr in node.log_attrs:
                print("\t{}: {}".format(attr, node.log_attrs[attr]))

    if args.events:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            print("Node {}:".format(node_id))
            for (_, e) in log.events[node.node_id].iterrows():
                print("\t{}\t{}".format(e.timestamp, e.event))

    if args.bad:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            for (_, pkt) in log.received[node.node_id].iterrows():
                if not pkt.header_valid:
                    print("HEADER INVALID: {}".format(pkt))
                elif not pkt.payload_valid:
                    print("PAYLOAD INVALID: {}".format(pkt))

    if args.received:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            for (_, pkt) in log.received[node.node_id].iterrows():
                print("Packet(seq={seq}, curhop={curhop}, nexthop={nexthop}, ms={ms}, fec0={fec0}, fec1={fec1}, size={size})".\
                      format(seq=pkt.seq, pkt=pkt.curhop, curhop=pkt.curhop, nexthop=pkt.nexthop, \
                             ms=pkt.ms, fec0=pkt.fec0, fec1=pkt.fec1, size=pkt.size))

    if args.dump_slot:
        if not args.node_id:
            print("Must specify node when dumping slot", file=sys.stderr)
            sys.exit(1)

        node = log.nodes[args.node_id]
        slot = log.findSlot(node, args.dump_slot)
        if slot is not None:
            if not args.output:
                args.output = 'slot.fc64'
            slot.iq_data.tofile(args.output)
        else:
            print("Slot not found at time {}.".format(args.dump_slot), file=sys.stderr)

if __name__ == '__main__':
    main()
