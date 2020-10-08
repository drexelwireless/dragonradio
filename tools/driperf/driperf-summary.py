#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import logging
import pandas
import re
import sys

def packetLoss(recv, send):
    """Calculate packet loss"""
    if len(recv.seq) == 0:
        return 1.
    else:
        return 1. - len(recv.seq)/send.npackets

def throughput(recv, send):
    """Calculate throughput in bps"""
    if len(recv.seq) == 0:
        return 0.
    elif len(recv.seq) == 1:
        return 0.
    else:
        return 8.*recv.datalen.sum()/(max(recv.timestamp) - min(recv.timestamp))

def outOfOrderPacket(recv, send):
    """Calculate how many packets were received out-of-order"""
    # Number of out of order packets
    if len(recv.seq) == 0:
        return 0

    ooo = 0
    max_seq = recv.seq.iloc[0]

    for i in range(1, len(recv.seq)):
        if recv.seq.iloc[i] < max_seq:
            ooo += 1
        max_seq = max(max_seq, recv.seq.iloc[i])

    return ooo

BPS = { 'unknown': 0

      # phase-shift keying
      , 'psk2'   : 1
      , 'psk4'   : 2
      , 'psk8'   : 3
      , 'psk16'  : 4
      , 'psk32'  : 5
      , 'psk64'  : 6
      , 'psk128' : 7
      , 'psk256' : 8

      # differential phase-shift keying
      , 'dpsk2'   : 1
      , 'dpsk4'   : 2
      , 'dpsk8'   : 3
      , 'dpsk16'  : 4
      , 'dpsk32'  : 5
      , 'dpsk64'  : 6
      , 'dpsk128' : 7
      , 'dpsk256' : 8

      # amplitude-shift keying
      , 'ask2'   : 1
      , 'ask4'   : 2
      , 'ask8'   : 3
      , 'ask16'  : 4
      , 'ask32'  : 5
      , 'ask64'  : 6
      , 'ask128' : 7
      , 'ask256' : 8

      # quadrature amplitude-shift keying
      , 'qam2'   : 1
      , 'qam4'   : 2
      , 'qam8'   : 3
      , 'qam16'  : 4
      , 'qam32'  : 5
      , 'qam64'  : 6
      , 'qam128' : 7
      , 'qam256' : 8

      # amplitude/phase-shift keying
      , 'apsk4'   : 2
      , 'apsk8'   : 3
      , 'apsk16'  : 4
      , 'apsk32'  : 5
      , 'apsk64'  : 6
      , 'apsk128' : 7
      , 'apsk256' : 8

      # specific modem types
      , 'bpsk'    : 1
      , 'qpsk'    : 2
      , 'ook'     : 1
      , 'sqam32'  : 5
      , 'sqam128' : 7
      , 'V29'     : 4

      , 'arb16opt'  : 4
      , 'arb32opt'  : 5
      , 'arb64opt'  : 6
      , 'arb128opt' : 7
      , 'arb256opt' : 8

      , 'arb64vt' : 6
      }

def bps(test):
    return BPS[test.ms]

RATES = { 'none': 1.

        , 'unknown': 0.

        , 'rep3': 1./3.
        , 'rep5': 1./5.

        , 'h74' : 4./7.
        , 'h84' : 4./8.
        , 'h128': 8./12.

        , 'g2412': 1./2.

        , 'secded2216': 2./3.
        , 'secded3932': 4./5.
        , 'secded7264': 8./9.

        , 'v27'   : 1./2.
        , 'v27p23': 2./3.
        , 'v27p34': 3./4.
        , 'v27p45': 4./5.
        , 'v27p56': 5./6.
        , 'v27p67': 6./7.
        , 'v27p78': 7./8.

        , 'v29'   : 1./2.
        , 'v29p23': 2./3.
        , 'v29p34': 3./4.
        , 'v29p45': 4./5.
        , 'v29p56': 5./6.
        , 'v29p67': 6./7.
        , 'v29p78': 7./8.

        , 'v39': 1./3.

        , 'v615': 1./6.

        , 'rs8': 223./255.
        }

def rate(test):
    return RATES[test.fec0] * RATES[test.fec1]

def main():
    parser = argparse.ArgumentParser(description='Summarize recv.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('server_log', action='store',
                        metavar='SERVERLOG',
                        help='specify server log')
    parser.add_argument('client_log', action='store',
                        metavar='CLIENTLOG',
                        help='specify client log')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    recv = pandas.read_csv(args.server_log, comment='#')
    tests = pandas.read_csv(args.client_log, comment='#')

    def applyToReceived(f, row):
        return f(recv.loc[recv['test'] == row.test], row)

    tests['loss'] = tests.apply(lambda row: applyToReceived(packetLoss, row), axis=1)
    tests['throughput'] = tests.apply(lambda row: applyToReceived(throughput, row), axis=1)
    tests['oop'] = tests.apply(lambda row: applyToReceived(outOfOrderPacket, row), axis=1)
    tests['rate'] = tests.apply(rate, axis=1)
    tests['bps'] = tests.apply(bps, axis=1)
    tests['theoretical bps'] = tests.apply(lambda row: bps(row)*rate(row), axis=1)
    tests['effective bps'] = tests.apply(lambda row: bps(row)*rate(row)*(1.0 - row.loss), axis=1)
    tests.sort_values('effective bps', inplace=True)

    print(tests)

    return 0

if __name__ == '__main__':
    main()
