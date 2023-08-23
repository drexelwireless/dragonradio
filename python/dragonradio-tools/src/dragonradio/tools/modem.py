# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from typing import List, Optional, Tuple

import matplotlib.pyplot as plt
import numpy as np

import dragonradio.liquid
from dragonradio.liquid import FrameStats, MCS
from dragonradio.packet import Header
import dragonradio.signal
from dragonradio.signal import Resampler, resample, resample_and_filter
from dragonradio.tools.plot.radio import PSDPlot, WaveformPlot

HEADER_MCS: MCS = MCS('crc32', 'secded7264', 'h84', 'bpsk')
"""MCS used for packet headers"""

def modulate(hdr: Header, mcs: MCS, payload: bytes, cbw: float, Fc: float, Fs: float,
             resample: Resampler=resample,
             mod=None,
             numtaps:int=301,
             plot: bool=False) -> np.ndarray:
    """Modulate a packet using OFDM.

    Args:
        hdr (Header): packet header
        mcs (MCS): modulation and coding scheme for packet payload
        payload (bytes): packet payload
        cbw (float): channel bandwidth
        Fc (float): center frequency
        Fs (float): sampling rate
        resample (Resampler): resampler
        mod: Modulator

    Returns:
        Modulated and mixed signal.
    """
    if mod is None:
        mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)
    mod.payload_mcs = mcs

    sig = mod.modulate(hdr, payload)

    if plot:
        fig = WaveformPlot(*plt.subplots())
        fig.plot(sig, title='Modulated signal')

    # Resample
    resampled = resample_and_filter(sig, rate=Fs/cbw, theta=Fc/Fs, resample=resample, numtaps=numtaps)

    if plot:
        fig = PSDPlot(*plt.subplots(), nfft=1024)
        fig.plot(Fs, resampled, title="PSD of upsampled signal")

    return resampled

def demodulate(sig: np.ndarray, cbw: float, Fc: float, Fs: float,
               resample: Resampler=resample,
               demod=None,
               numtaps:int=301,
               plot: bool=False) -> List[Tuple[Header, bytes, FrameStats]]:
    """Demodulate an OFDM signal.

    Args:
        sig: signal to demodulate
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    # Resample
    resampled = resample_and_filter(sig, rate=cbw/Fs, theta=Fc/Fs, resample=resample, numtaps=numtaps)

    # Plot resampled signal
    if plot:
        fig = PSDPlot(*plt.subplots(), nfft=1024)
        fig.plot(cbw, resampled, title='PSD of downsampled Signal')

        fig = WaveformPlot(*plt.subplots())
        fig.plot(resampled, title='Downsampled Signal')

    # Demodulate mixed, down-sampled signal
    if demod is None:
        demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, False, 48, 6, 4)

    return demod.demodulate(resampled)
