#!/usr/bin/env python3
import argparse
import matplotlib as mp
from matplotlib.text import OffsetFrom
from matplotlib.widgets import Button, Slider
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import sys

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
    parser.add_argument('--bad', action='store_true',
                        help='list bad packets')
    parser.add_argument('--dump-slot', action='store', type=float,
                        help='dump samples from given slot')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    log = drlog.Log()

    for path in args.paths:
        log.load(path)

    if args.header:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            print("Node {}:".format(node_id))
            print("\t{}".format(node.start))
            for attr in node.log_attrs:
                print("\t{}: {}".format(attr, node.log_attrs[attr]))

    if args.bad:
        for node_id in log.nodes:
            node = log.nodes[node_id]
            for pkt in log.received[node.node_id]:
                if not pkt.hdr_valid:
                    print("HEADER INVALID: {}".format(pkt))
                elif not pkt.payload_valid:
                    print("PAYLOAD INVALID: {}".format(pkt))

    if args.dump_slot:
        if not args.node_id:
            print("Must specify node when dumping slot", file=sys.stderr)
            sys.exit(1)

        node = log.nodes[args.node_id]
        slot = log.findSlot(node, args.dump_slot)
        if slot:
            if not args.output:
                args.output = 'slot.fc64'
            slot.data.tofile(args.output)
        else:
            print("Slot not found at time {}.".format(args.dump_slot), file=sys.stderr)

if __name__ == '__main__':
    main()
