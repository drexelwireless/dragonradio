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

def checkBandwidth(args):
    if not args.bandwidth:
        print("Must specify bandwidth when plotting", file=sys.stderr)
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description='Dump DragonRadio log info.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('-s', '--specgram', action='store_true',
                        help='plot spectrogram')
    parser.add_argument('-w', '--waveform', action='store_true',
                        help='plot waveform')
    parser.add_argument('-b', '--bandwidth', action='store', type=float,
                        help='specify sample bandwidth')
    parser.add_argument('path')
    args = parser.parse_args()

    sig = np.fromfile(args.path, dtype=np.complex64)

    if args.specgram:
        checkBandwidth(args)

        fig = drgui.SpecgramPlot(*plt.subplots())
        fig.plot(args.bandwidth, sig, 0)
        plt.show()

    if args.waveform:
        fig = drgui.WaveformPlot(*plt.subplots())
        fig.plot(sig)
        plt.show()

if __name__ == '__main__':
    main()
