# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import math

import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal

import dragonradio.liquid
import dragonradio.signal
from dragonradio.tools.plot.radio import PSDPlot

HEADER_MCS = dragonradio.liquid.MCS('crc32', 'secded7264', 'h84', 'bpsk')
"""MCS used for packet headers"""

def lowpass(wp, ws, fs, atten=60):
    """Design a lowpass filter using a Kaiser window.

    Args:
        wp: Passband frequency
        ws: Stopband frequency
        fs: Sample rate
        atten: desired attenuation (dB)

    Returns:
        Filter taps.
    """
    N, beta = signal.kaiserord(atten, (ws-wp)/fs)
    if N % 2 == 0:
        N += 1

    return signal.firwin(N, ws/2,
                         window=('kaiser', beta),
                         fs=fs,
                         pass_zero=True,
                         scale=True)

def modulate(hdr, mcs, payload, cbw, Fc, Fs):
    """Modulate a packet using OFDM.

    Args:
        hdr: packet header
        mcs: modulation and coding scheme for packet payload
        payload: packet payload
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)
    mod.payload_mcs = mcs

    sig = mod.modulate(hdr, payload)

    # Upsample
    wp = cbw-100e3
    ws = cbw+100e3

    upsamp = dragonradio.signal.RationalResamplerCCC(Fs/cbw)
    upsamp.taps = lowpass(wp, ws, upsamp.down_rate*Fs, atten=90)

    upsampled = upsamp.resample(np.append(sig, np.zeros(math.ceil(upsamp.delay))))
    upsampled = upsampled[math.floor(upsamp.rate*upsamp.delay):]

    # Frequency shift
    nco = dragonradio.signal.TableNCO(2*math.pi*Fc/Fs)
    mixed = nco.mix_up(upsampled)

    return mixed

def demodulate(sig: np.ndarray, cbw: float, Fc: float, Fs: float, plot=False):
    """Demodulate an OFDM signal.

    Args:
        sig: signal to demodulate
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    # Frequency shift
    nco = dragonradio.signal.TableNCO(2*math.pi*Fc/Fs)
    mixed = nco.mix_down(sig)

    # Downsample
    wp = cbw-100e3
    ws = cbw+100e3

    downsamp = dragonradio.signal.RationalResamplerCCC(cbw/Fs)
    downsamp.taps = lowpass(wp, ws, downsamp.up_rate*Fs, atten=90)

    downsampled = downsamp.resample(np.append(mixed, np.zeros(math.ceil(downsamp.delay))))
    downsampled = downsampled[math.floor(downsamp.rate*downsamp.delay):]

    # Plot PSD of downsampled signal
    if plot:
        fig = PSDPlot(*plt.subplots(), nfft=1024)
        fig.plot(cbw, downsampled, title='PSD of downsampled Signal')

    # Demodulate mixed, down-sampled signal
    demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, False, 48, 6, 4)

    return demod.demodulate(downsampled)

def modulateMix(hdr, mcs, payload, cbw, Fc, Fs):
    """Modulate a packet using OFDM and mixing resampler.

    Args:
        hdr: packet header
        mcs: modulation and coding scheme for packet payload
        payload: packet payload
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)
    mod.payload_mcs = mcs

    sig = mod.modulate(hdr, payload)

    # Up-sample and mix up in one operation
    wp = cbw-100e3
    ws = cbw+100e3

    fshift = 2*math.pi*Fc/Fs
    taps = lowpass(wp, ws, Fs, atten=90)
    upsamp = dragonradio.signal.MixingRationalResamplerCCC(Fs/cbw, fshift, taps)

    upsampled = upsamp.resampleMixUp(np.append(sig, np.zeros(math.ceil(upsamp.delay))))
    upsampled = upsampled[math.floor(upsamp.rate*upsamp.delay):]

    return upsampled

def demodulateMix(sig, cbw, Fc, Fs, plot=False):
    """Demodulate an OFDM signal using mixing resampler.

    Args:
        sig: signal to demodulate
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    # Downsample and mix down in one operation
    wp = cbw-100e3
    ws = cbw+100e3

    fshift = 2*math.pi*Fc/Fs
    taps = lowpass(wp, ws, Fs, atten=90)
    downsamp = dragonradio.signal.MixingRationalResamplerCCC(cbw/Fs, fshift, taps)

    downsampled = downsamp.resampleMixDown(np.append(sig, np.zeros(math.ceil(downsamp.delay))))
    downsampled = downsampled[math.floor(downsamp.rate*downsamp.delay):]

    # Plot PSD of modulated signal
    if plot:
        fig = PSDPlot(*plt.subplots(), nfft=1024)
        fig.plot(Fs, downsampled, title='PSD of Downsampled Signal')

    # Demodulate mixed, down-sampled signal
    demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, False, 48, 6, 4)

    return demod.demodulate(downsampled)

# Length of filter
P = 25*128+1

# Length of FFT
N = 4*(P-1)

# Number of new samples consumed per input block
L = N - (P - 1)

# Overlap factor
V = N/(P - 1)

def modulateFast(hdr, mcs, payload, cbw, Fc, Fs, mod=None):
    """Modulate a packet using OFDM and interpolate and mix in frequency domain..

    Args:
        hdr: packet header
        mcs: modulation and coding scheme for packet payload
        payload: packet payload
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    # Modulate signal
    if mod is None:
        mod = dragonradio.liquid.OFDMModulator(HEADER_MCS, 48, 6, 4)
    mod.payload_mcs = mcs

    sig = mod.modulate(hdr, payload)

    # Number of FFT bins to rotate
    Nrot = int(N*Fc/Fs)

    # Interpolation factor
    U = int(Fs/cbw)

    # Perform mixing, convolution, and interpolation in frequency space
    off = 0
    upsampled = np.zeros(0)

    while off < len(sig):
        # Buffer source block
        if off == 0:
            # Handle first block by inserting zeros
            x = np.append(np.zeros((P-1)//U), sig[:L//U])
            off = (L-(P-1))//U
        else:
            x = sig[off:off+N//U]
            off += L//U

        # Perform FFT
        X = np.fft.fft(x, N//U)

        # Interpolate
        XU = np.zeros(N, dtype='complex128')
        XU[:N//U//2] = X[:N//U//2]
        XU[N-(N//U//2):] = X[N//U-(N//U//2):]

        # Mix by rotating FFT bins
        XU = np.roll(XU, Nrot)

        # Convolve
        X2 = XU

        # Perform inverse FFT
        y = np.fft.ifft(XU, N)

        upsampled = np.append(upsampled, y[(P-1):N])

    # Compensate for upsampling by multiplying by Fs/cbw
    return upsampled*Fs/cbw

def demodulateFast(sig, cbw, Fc, Fs, plot=False, demod=None):
    """Demodulate an OFDM signal using frequency-domain filtering.

    Args:
        sig: signal to demodulate
        cbw: channel bandwidth
        Fc: center frequency
        Fs: signal rates

    Returns:
        Modulated and mixed signals.
    """
    # Compute lowpass filter
    wp = cbw-100e3
    ws = cbw+100e3

    h = lowpass(wp, ws, Fs, atten=90)

    # Compute frequency-space filter
    H = np.fft.fft(h, N)

    # Number of FFT bins to rotate
    Nrot = int(N*Fc/Fs)

    # Decimation factor
    D = int(Fs/cbw)

    # Perform mixing, convolution, and decimation in frequency space
    off = 0
    downsampled = np.zeros(0)

    while off < len(sig):
        # Buffer source block
        if off == 0:
            # Handle first block by inserting zeros
            x = np.append(np.zeros(P-1), sig[:L])
            off = L-(P-1)
        else:
            x = sig[off:off+N]
            off += L

        # Perform FFT
        X = np.fft.fft(x, N)

        # Mix by rotating FFT bins
        X = np.roll(X, -Nrot)

        # Convolve
        X2 = X * H

        # Decimate
        XD = np.zeros(N//D, dtype='complex128')

        for i in range (0, D):
            XD += X2[i*N//D:(i+1)*N//D]

        y = np.fft.ifft(XD, N//D)

        downsampled = np.append(downsampled, y[(P-1)//D:N//D])

    # Correct for tail end of signal
    downsampled = downsampled[:len(sig)//D]

    # Plot PSD of modulated signal
    if plot:
        fig = PSDPlot(*plt.subplots(), nfft=1024)
        fig.plot(Fs, downsampled, title='PSD of Downsampled Signal')

    # Demodulate mixed, down-sampled signal
    if demod is None:
        demod = dragonradio.liquid.OFDMDemodulator(HEADER_MCS, True, False, 48, 6, 4)

    return demod.demodulate(downsampled)
