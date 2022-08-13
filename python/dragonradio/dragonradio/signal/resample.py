# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from fractions import Fraction
import math
from typing import Callable

import numpy as np
from numpy.typing import ArrayLike

import dragonradio.signal

Resampler = Callable[[int, int, ArrayLike, ArrayLike, float], np.ndarray]
"""The type of a resampler"""

def resample_and_filter(sig: ArrayLike, rate: float, fshift: float, resample: Resampler, numtaps: int, ftype: str='ls'):
    if rate == 1 and fshift == 0:
        return sig

    # Determine interpolation and decimation rates
    frate = Fraction(rate).limit_denominator(200)

    # Determine stopband in normalized frequency units
    ws = min(1.0/frate.numerator, 1.0/frate.denominator)

    # Create lowpass filter
    h = dragonradio.signal.lowpass(wp=0.95*ws, ws=ws, fs=1.0, numtaps=numtaps, ftype=ftype)

    return resample(frate.numerator, frate.denominator, h, sig, fshift=fshift)

def resample(p: int, q: int, h: ArrayLike, sig: ArrayLike, fshift: float=0) -> np.ndarray:
    resampler = dragonradio.signal.RationalResamplerCCC(p/q)
    resampler.taps = h

    if fshift != 0 and p/q < 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(fshift)
        sig = nco.mix_down(sig)

    # Append samples to compensate for filter delay
    resampled = resampler.resample(np.append(sig, np.zeros(math.ceil(resampler.delay))))

    # Remove prefix consisting of transient samples
    resampled = resampled[math.floor(resampler.rate*resampler.delay):]

    if fshift != 0 and p/q > 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(fshift)
        resampled = nco.mix_up(resampled)

    return resampled

def resample_and_mix(p: int, q: int, h: ArrayLike, sig: ArrayLike, fshift: float=0) -> np.ndarray:
    resampler = dragonradio.signal.MixingRationalResamplerCCC(p/q, fshift, h)

    # Append samples to compensate for filter delay
    if p/q > 1.0:
        resampled = resampler.resampleMixUp(np.append(sig, np.zeros(math.ceil(resampler.delay))))
    else:
        resampled = resampler.resampleMixDown(np.append(sig, np.zeros(math.ceil(resampler.delay))))

    # Remove prefix consisting of transient samples
    return resampled[math.floor(resampler.rate*resampler.delay):]

def fdupsample(U: int, D: int, _h: ArrayLike, sig: ArrayLike, fshift: float=0, P: int=25*128+1) -> np.ndarray:
    if D != 1:
        raise ValueError("fdupsample: can only upsample")

    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N - (P-1)

    # Number of FFT bins to rotate
    Nrot = int(N*(fshift/(2*math.pi)))

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

        # Perform inverse FFT
        y = np.fft.ifft(XU, N)

        upsampled = np.append(upsampled, y[(P-1):N])

    # Compensate for upsampling by multiplying by interpolation factor
    return upsampled*U

def fddownsample(U: int, D: int, h: ArrayLike, sig: ArrayLike, fshift: float=0, P: int=25*128+1) -> np.ndarray:
    if U != 1:
        raise ValueError("fddownsample: can only downsample")

    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N - (P-1)

    # Compute frequency-space filter
    H = np.fft.fft(h, N)

    # Number of FFT bins to rotate
    Nrot = int(N*(fshift/(2*math.pi)))

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
    return downsampled[:len(sig)//D]
