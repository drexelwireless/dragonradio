# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from fractions import Fraction
import math
from typing import Callable, Optional

import numpy as np
from numpy.typing import ArrayLike

import dragonradio.signal

Resampler = Callable[[int, int, ArrayLike, ArrayLike, float], np.ndarray]
"""The type of a resampler"""

def resample_and_filter(sig: ArrayLike, rate: float, theta: float, resample: Resampler, numtaps: int, ftype: str='ls'):
    if rate == 1 and theta == 0:
        return sig

    # Determine interpolation and decimation rates
    frate = Fraction(rate).limit_denominator(200)

    # Determine stopband in normalized frequency units
    ws = min(1.0/frate.numerator, 1.0/frate.denominator)

    # Create lowpass filter
    h = dragonradio.signal.lowpass(wp=0.95*ws, ws=ws, fs=1.0, numtaps=numtaps, ftype=ftype)

    return resample(frate.numerator, frate.denominator, h, sig, theta)

def upsample(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    if q != 1:
        raise ValueError("upsample: can only upsample")

    if theta != 0:
        raise ValueError("upsample: cannot frequency shift")

    resampler = dragonradio.signal.UpsamplerCCC(p)
    resampler.taps = h

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//q:]

    return resampled

def downsample(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    if p != 1:
        raise ValueError("downsample: can only downsample")

    if theta != 0:
        raise ValueError("downsample: cannot frequency shift")

    resampler = dragonradio.signal.DownsamplerCCC(q)
    resampler.taps = h

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//q:]

    return resampled

def resample(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    resampler = dragonradio.signal.RationalResamplerCCC(p/q)
    resampler.taps = h

    if theta != 0 and p/q < 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(2*math.pi*theta)
        sig = nco.mix_down(sig)

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//q:]

    if theta != 0 and p/q > 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(2*math.pi*theta)
        resampled = nco.mix_up(resampled)

    return resampled

def resample_and_mix(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    resampler = dragonradio.signal.MixingRationalResamplerCCC(p/q, theta, h)

    # Append samples to compensate for filter delay
    delay = int(resampler.delay)

    if p/q > 1.0:
        resampled = resampler.resampleMixUp(np.append(sig, np.zeros(delay)))
    else:
        resampled = resampler.resampleMixDown(np.append(sig, np.zeros(delay)))

    # Remove prefix consisting of transient samples
    return resampled[delay//q:]

def fdupsample(U: int, D: int, _h: ArrayLike, sig: ArrayLike, theta: float=0, P: int=25*128+1) -> np.ndarray:
    if D != 1:
        raise ValueError("fdupsample: can only upsample")

    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N - (P-1)

    # Number of FFT bins to rotate
    Nrot = int(N*theta)

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

def fddownsample(U: int, D: int, h: ArrayLike, sig: ArrayLike, theta: float=0, P: int=25*128+1) -> np.ndarray:
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
    Nrot = int(N*theta)

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

def fdresample(U: int, D: int, h: Optional[np.ndarray], sig: np.ndarray, theta: float=0, P: int=2**7*3*5**3*7+1):
    # Overlap factor
    V = 4

    # Length of FFT
    N = V*(P-1)

    # Number of new samples consumed per input block
    L = N-(P-1)

    # Validate rates
    if N % U != 0:
        raise ValueError(f"Interpolation rate must divide FFT size, but {U:} does not divide {N:}")

    if N % D != 0:
        raise ValueError(f"Decimation rate must divide FFT size, but {D:} does not divide {N:}")

    # Compute and validate number of frequency bins to rotate
    Nrot = N*theta

    if U/D < 1.0:
        Nrot /= U
    elif U/D > 1.0:
        Nrot /= D

    Nrot_int = round(Nrot)
    if abs(Nrot - Nrot_int) > 1e-5:
        raise ValueError(f"Frequency shift must consist of integer number of FFT bins, but {Nrot:} is not an integer")

    Nrot = Nrot_int

    # Compute compensation factor. We compensate for interpolation by
    # multiplying by U and compensate for repeating the signal D times during
    # downsampling by dividing by D.
    k = U/D

    # Compute frequency-space filter
    if h is not None:
        H = k*np.fft.fft(h, N)
    else:
        H = None

    # Perform mixing, convolution, and interpolation in frequency space
    off = 0
    resampled = np.zeros(0)

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

        if U/D < 1.0:
            X = np.roll(X, -Nrot)

        # Interpolate
        XU = np.zeros(N, dtype='complex128')

        if h is not None:
            # Duplicate
            l = N//U
            l2 = l//2

            for i in range(0, U):
                XU[i*l:i*l+l2] = X[:l2]
                XU[N-i*l-l2:N-i*l] = X[l2:]

            # Convolve
            XU = XU*H
        else:
            XU[:N//U//2] = X[:N//U//2]
            XU[N-(N//U//2):] = X[N//U-(N//U//2):]

        # Decimate
        XD = np.zeros(N//D, dtype='complex128')

        for i in range (0, D):
            XD += XU[i*N//D:(i+1)*N//D]

        # Mix up by rotating FFT bins
        if U/D > 1.0:
            XD = np.roll(XD, Nrot)

        # Perform inverse FFT
        y = np.fft.ifft(XD, N//D)

        resampled = np.append(resampled, y[(P-1)//D:N//D])

    # Compensate for resampling if we didn't filter
    if H is None:
        resampled *= k

    # Correct for filter delay and tail end of signal
    if h is None:
        delay = 0
    else:
        delay = (len(h)-1)//2

    return resampled[delay//D:(delay+len(sig)*U)//D]
