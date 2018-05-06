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

def main():
    global viewer

    parser = argparse.ArgumentParser(description='Show received packets.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    log = drlog.Log()

    for path in args.paths:
        log.load(path)

    for node_id in log.nodes:
        node = log.nodes[node_id]
        for pkt in log.received[node.node_id]:
            if not pkt.hdr_valid:
                print("HEADER INVALID: {}".format(pkt))
            elif not pkt.payload_valid:
                print("PAYLOAD INVALID: {}".format(pkt))

if __name__ == '__main__':
    main()
