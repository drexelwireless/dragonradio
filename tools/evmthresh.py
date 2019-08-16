#!/usr/bin/env python
import sys
sys.path.insert(0, '..')

import argparse
import logging
import math
import matplotlib as mp
import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
import scipy.signal as signal

import dragonradio

import drlog
import drgui

def main():
    parser = argparse.ArgumentParser(description='Simulate and compute EVM thresholds',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--plot', action='store_true',
                        help='plot CDF of PER vs. EVM')
    parser.add_argument('--threshold', type=float,
                        help='report EVM thresholds for given PER')
    parser.add_argument('paths', nargs='*')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    log = drlog.Log()

    for path in args.paths:
        log.load(path)

    # Load all received packets into a data frame
    df = pd.concat([log.received[node_id] for node_id in log.nodes], ignore_index=True)

    # Add a boolean flag for packets that were successfully received
    df['received'] = (df.header_valid == 1) & (df.payload_valid == 1)

    # Group by MCS and sort by EVM within groups
    df = df.groupby(['crc', 'fec0', 'fec1', 'ms'], as_index=False).apply(lambda x: x.sort_values(by=['evm']))

    # Compute CDF of probability of being received
    grp = df.groupby(['crc', 'fec0', 'fec1', 'ms'])
    df['nsent'] = 1 + grp.cumcount()
    df['nreceived'] = grp['received'].transform(pd.Series.cumsum)
    df['prob_received'] = df['nreceived'] / df['nsent']

    mcss = [(crc, fec0, fec1, ms, dragonradio.MCS(crc, fec0, fec1, ms).rate) for (crc, fec0, fec1, ms) in grp.groups.keys()]
    mcss.sort(key=lambda x: x[4])
    df.to_csv('recv.csv')

    # Compute EVM thresholds
    if args.threshold:
        for (crc, fec0, fec1, ms, rate) in mcss:
            df_ms = df[(df.crc == crc) & (df.fec0 == fec0) & (df.fec1 == fec1) & (df.ms == ms)]
            max_evm = df_ms.loc[df.prob_received > args.threshold].evm.max()
            print("{},{},{},{:1.1f},{:f}".format(ms, fec0, fec1, rate, max_evm))

    # Plot results:
    if args.plot:
        fig = plt.figure(num='Probability of Reception vs. EVM')
        ax = fig.add_subplot(1,1,1)

        for (crc, fec0, fec1, ms, rate) in mcss:
            df_ms = df[(df.crc == crc) & (df.fec0 == fec0) & (df.fec1 == fec1) & (df.ms == ms)]
            ax.plot(df_ms.evm, df_ms.prob_received, label="{},{},{} ({:1.1f})".format(ms, fec0, fec1, rate))

        ax.set_xlabel('EVM')

        ax.set_ylabel('Pr(received)')
        ax.set_ylim(0, 1)
        ax.legend()
        plt.show()

if __name__ == '__main__':
    main()
