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

    resampled = resampler.resample(np.append(sig, np.zeros(delay//p)))

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

    resampled = resampler.resample(np.append(sig, np.zeros(delay//p)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//q:]

    return resampled

def resample(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    resampler = dragonradio.signal.RationalResamplerCCC(p, q, h)

    if theta != 0 and p/q < 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(2*math.pi*theta)
        sig = nco.mix_down(sig)

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay//p)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//q:]

    if theta != 0 and p/q > 1.0:
        # Frequency shift
        nco = dragonradio.signal.TableNCO(2*math.pi*theta)
        resampled = nco.mix_up(resampled)

    return resampled

def resample_and_mix(p: int, q: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    resampler = dragonradio.signal.MixingRationalResamplerCCC(p, q, theta, h)

    # Append samples to compensate for filter delay
    delay = int(resampler.delay)

    if p/q > 1.0:
        resampled = resampler.resampleMixUp(np.append(sig, np.zeros(delay//p)))
    else:
        resampled = resampler.resampleMixDown(np.append(sig, np.zeros(delay//p)))

    # Remove prefix consisting of transient samples
    return resampled[delay//q:]

def fdupsample(U: int, D: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    if D != 1:
        raise ValueError("fdupsample_native: can only upsample")

    resampler = dragonradio.signal.FDUpsamplerCCC(1, U, theta)

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay//U)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//D:]

    return resampled

def fddownsample(U: int, D: int, h: ArrayLike, sig: ArrayLike, theta: float=0) -> np.ndarray:
    if U != 1:
        raise ValueError("fddownsample: can only downsample")

    resampler = dragonradio.signal.FDDownsamplerCCC(1, D, theta, h)

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay//U)))

    # Remove prefix consisting of transient samples
    resampled = resampled[delay//D:]

    return resampled

def fdresample(U: int, D: int, h: ArrayLike, sig: ArrayLike, theta: float=0, exact: bool=False) -> np.ndarray:
    resampler = dragonradio.signal.FDResamplerCCC(U, D, 1, theta, h)
    resampler.exact = exact

    # Append samples to compensate for filter
    delay = int(resampler.delay)

    resampled = resampler.resample(np.append(sig, np.zeros(delay//U)))

    # Correct for filter delay and tail end of signal
    return resampled[delay//D:]

def fdresample_exact(*args, exact=True, **kwargs):
    return fdresample(*args, exact=exact, **kwargs)
