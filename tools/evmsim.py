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

import dragonsignal

HEADER_MCS = dragonradio.liquid.MCS('crc32', 'none', 'v29p78', 'bpsk')

def float_dtype(dt):
    if dt == np.complex64:
        return np.float32
    elif dt == np.complex128:
        return np.float64
    else:
        raise Exception("Cannot find float dtype for dtype %s" % str(dt))

def maxAmp(sig):
    """Find the maximum absolute value of the amplitudes of the real and imaginary parts of a signal"""
    return np.sqrt(np.max(sig.real ** 2 + sig.imag ** 2))

def clipSig(sig):
    """Clip an IQ signal so that its real and imaginary parts have magintude <= 1"""
    return np.clip(sig.view(float_dtype(sig.dtype)), -1, 1).view(sig.dtype)

def sigPower(sig):
    """Compute average power of a signal in dB"""
    return 10 * np.log10(np.mean(sig.real**2 + sig.imag**2))

def awgn(sig, snr=None, db=None):
    """Add additive white Gaussian noise at given SNR"""
    if snr is not None:
        # Compute average power in dB
        sig_avg_db = sigPower(sig)

        # Compute noise dB
        noise_avg_db = sig_avg_db - snr
    elif db is not None:
        noise_avg_db = db
    else:
        raise Exception("awgn: must specify either snr or db")

    # Compute average noise power
    noise_avg_power = 10 ** (noise_avg_db / 10)

    # Return WGN
    return np.random.normal(0, np.sqrt(noise_avg_power), len(sig))

def simulateMCS(crc, fec0, fec1, ms, pathloss_db=0, snr=None, db=None, pkt_size=1500, random=True, ntrials=100, Fs=10e6, cbw=1e6, Fc=4.5e6, mod=None, demod=None):
    """Simulate a single MCS"""
    if mod is None:
        mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)

    if demod is None:
        demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, True, 48, 6, 4)

    mcs = dragonradio.liquid.MCS('crc32', 'rs8', 'v29p78', ms)

    # Compute multiplicative "gain" from pathloss dB
    g = dragonsignal.dB2gain(-pathloss_db)

    # We use the same header for every trial
    hdr = dragonradio.Header(1, 2, 0)

    if not random:
        # Default payload is all-NULL bytes
        payload = b'0' * pkt_size

        # Modulate the packet
        sig = dragonsignal.modulateFast(hdr, mcs, payload, cbw, Fc, Fs, mod=mod)

        # Calculate normalization factor
        A = maxAmp(sig)

        # Normalize the signal
        sig = sig/A

    results = []

    for i in range(0, ntrials):
        if random:
            payload = np.random.bytes(pkt_size)

            # Modulate the packet
            sig = dragonsignal.modulateFast(hdr, mcs, payload, cbw, Fc, Fs, mod=mod)

            # Calculate normalization factor
            A = maxAmp(sig)

            # Normalize the signal
            sig = sig/A

        # Apply noise and pathloss
        rx_sig = g*sig + awgn(sig, snr=snr, db=db)

        # Attempt to demodulate
        demod.reset()
        pkts = dragonsignal.demodulateFast(rx_sig, cbw, Fc, Fs, demod=demod)
        if len(pkts) > 0:
            (_hdr, pkt, stats) = pkts[0]
            results.append((crc, fec0, fec1, ms, A, pathloss_db, snr, pkt is not None, stats.evm, stats.rssi))
        else:
            results.append((crc, fec0, fec1, ms, A, pathloss_db, snr, False, None, None))

    return results

def simulate(amc_table, snrs=None, **kwargs):
    results = []

    for (crc, fec0, fec1, ms) in amc_table:
        logging.info("Simulating {} {} {} {}".format(crc, fec0, fec1, ms))
        for snr in snrs:
            results += simulateMCS(crc, fec0, fec1, ms, snr=snr, **kwargs)

    df = pd.DataFrame(results,
                      columns=['crc',
                               'fec0',
                               'fec1',
                               'ms',
                               'A',
                               'pathloss_db',
                               'snr_db',
                               'received',
                               'evm',
                               'rssi'])

    df = df.astype({ 'crc': str
                   , 'fec0': str
                   , 'fec1': str
                   , 'ms': str
                   , 'A': np.float32
                   , 'pathloss_db': np.float32
                   , 'snr_db': np.float32
                   , 'received': bool
                   , 'evm': np.float32
                   , 'rssi': np.float32
                   })

    return df

def readSimulationResults(path):
    """Read simulation results from a CSV file, sort by EVM, and calculate loss."""
    df = pd.read_csv(path, index_col=0)
    df.dropna(subset=['evm'], inplace=True)

    df = df.groupby(['crc', 'fec0', 'fec1', 'ms'], as_index=False).apply(lambda x: x.sort_values(by=['evm']))
    grp = df.groupby(['crc', 'fec0', 'fec1', 'ms'])
    df['nsent'] = 1 + grp.cumcount()
    df['nreceived'] = grp['received'].transform(pd.Series.cumsum)
    df['prob_received'] = df['nreceived'] / df['nsent']

    return df

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
    parser.add_argument('-o', '--output', type=str,
                        help='set output')
    parser.add_argument('--simulate', action='store_true',
                        help='run simulation')
    parser.add_argument('--ntrials', type=int,
                        default=1,
                        help='set number of trials per MCS/SNR combination')
    parser.add_argument('--plot', type=str,
                        help='plot results')
    parser.add_argument('--threshold', type=str,
                        help='report EVM thresholds for packet loss rate')
    parser.add_argument('--per', type=float,
                        default=0.99,
                        help='set packet loss rate threshold')
    parser.add_argument('--soft-gain', type=str,
                        help='report soft gain factors needed for 0 dBFS')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    amc_table = [ ('crc32', 'secded7264', 'h84', 'bpsk')
                , ('crc32', 'rs8', 'none', 'bpsk')
                , ('crc32', 'rs8', 'none', 'qpsk')
                , ('crc32', 'rs8', 'none', 'qam8')
                , ('crc32', 'rs8', 'none', 'qam16')
                , ('crc32', 'rs8', 'none', 'qam32')
                , ('crc32', 'rs8', 'none', 'qam64')
                , ('crc32', 'rs8', 'none', 'qam128')
                , ('crc32', 'rs8', 'none', 'qam256')
                ]

    snrs = range(0, 51, 1)

    # Run simulation
    if args.simulate:
        mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)
        demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, True, 48, 6, 4)

        df = simulate(amc_table, snrs=snrs, pathloss_db=20, ntrials=args.ntrials, mod=mod, demod=demod)

        if args.output:
            df.to_csv(args.output)

    # Plot results:
    if args.plot:
        df = readSimulationResults(args.plot)

        fig = plt.figure(num='Simulation: {}'.format(os.path.basename(args.plot)))
        ax = fig.add_subplot(1,1,1)

        for (crc, fec0, fec1, ms) in amc_table:
            rate = dragonradio.MCS(crc, fec0, fec1, ms).rate
            df_ms = df[(df.crc == crc) & (df.fec0 == fec0) & (df.fec1 == fec1) & (df.ms == ms)]
            ax.plot(df_ms.evm, df_ms.prob_received, label="{},{},{} ({:1.1f})".format(ms, fec0, fec1, rate))

        ax.set_xlabel('EVM')

        ax.set_ylabel('Pr(received)')
        ax.set_ylim(0, 1)
        ax.legend()
        plt.show()

    # Compute EVM thresholds
    if args.threshold:
        df = readSimulationResults(args.threshold)

        for (crc, fec0, fec1, ms) in amc_table:
            df_ms = df[(df.crc == crc) & (df.fec0 == fec0) & (df.fec1 == fec1) & (df.ms == ms)]
            max_evm = df_ms.loc[df.prob_received > args.per].evm.max()
            rate = dragonradio.liquid.MCS(crc, fec0, fec1, ms).rate
            print("{},{},{},{:1.1f},{:f}".format(ms, fec0, fec1, rate, max_evm))

    # Compute soft gain thresholds
    if args.soft_gain:
        df = readSimulationResults(args.soft_gain)

        for (crc, fec0, fec1, ms) in amc_table:
            df_ms = df[(df.crc == crc) & (df.fec0 == fec0) & (df.fec1 == fec1) & (df.ms == ms)]
            As = df_ms.A
            rate = dragonradio.liquid.MCS(crc, fec0, fec1, ms).rate
            print("{},{},{},{:1.1f},{:f},{:f}".format(ms, fec0, fec1, rate, dragonsignal.gain2dB(1/As.mean()), dragonsignal.gain2dB(1/As.max())))

if __name__ == '__main__':
    main()
