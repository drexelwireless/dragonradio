# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Filter design"""
from functools import lru_cache
import math
import numpy as np
import scipy.signal as signal

import dragonradio.radio

def bellangerord(delta1, delta2, fs, deltaf):
    """Estimate filter order.

    Uses equation (5.32) from Bellanger to estimate filter order.

    Args:
        delta1: Passband ripple
        delta2: Stopband ripple
        fs: Sample rate
        deltaf: transition bandwidth

    Returns:
        Filter order
    """
    return int(math.ceil(2/3*math.log(1/(10*delta1*delta2))*fs/deltaf))

def remez1f(numtaps, bands, desired,
            weight=None,
            Hz=None,
            ftype='bandpass',
            maxiter=25,
            grid_density=16,
            fs=None):
    """Calculate the minimax optimal filter using the Remez exchange algorithm,
    but with $1/f$ rolloff.

    See:
      https://dsp.stackexchange.com/questions/37709/convert-a-park-mcclellan-fir-solution-to-achieve-stop-band-roll-off
    """
    taps = signal.remez(numtaps, bands, desired, weight=weight, Hz=Hz, type=ftype, maxiter=maxiter, grid_density=grid_density, fs=fs)

    # Adjust to get roll-off.
    taps[0] = taps[1] / 2
    taps[numtaps-1] = taps[numtaps - 2]/2

    # Re-normalize taps
    return taps / np.sum(taps)

def firpm1f(N, wp, ws, fs):
    """Use firpm library to calculate minimax optimal low-pass filter with $1/f$ rolloff.

    Args:
        N: number of taps
        wp: Passband frequency
        ws: Stopband frequency
        fs: Sample rate

    Returns:
        Filter taps.
    """
    bands = np.array([0, wp/2, ws/2, fs/2])
    desired = [1, 1, 0, 0]
    weights = [1, 1]

    out = dragonradio.radio.firpm1f(N, bands, desired, weights, fs=fs)

    return out.h

def firpm1f2(N, wp, ws, fs):
    """Use firpm library to calculate minimax optimal low-pass filter with $1/{f^2}$ rolloff.

    Args:
        N: number of taps
        wp: Passband frequency
        ws: Stopband frequency
        fs: Sample rate

    Returns:
        Filter taps."""
    bands = np.array([0, wp/2, ws/2, fs/2])
    desired = [1, 1, 0, 0]
    weights = [1, 1]

    out = dragonradio.radio.firpm1f2(N, bands, desired, weights, fs=fs)

    return out.h

@lru_cache
def lowpass(wp, ws, fs, ftype='kaiser', atten=60, Nmax=301):
    """Calculate a low-pass filter, estimating the number of taps needed.

    Args:
        wp: Passband frequency
        ws: Stopband frequency
        ftype: Type of filter. One of 'kaiser', 'firpm1f', or 'firpm1f2'
        atten: desired attenuation (dB)
        Nmax: Maximum number of taps

    Returns:
        Filter taps.
        """
    # Use Kaiser window
    if ftype == 'kaiser':
        N, beta = signal.kaiserord(atten, (ws-wp)/fs)
        N = min(Nmax, N)
        if N % 2 == 0:
            N += 1

        return signal.firwin(N, ws/2,
                             window=('kaiser', beta),
                             fs=fs,
                             pass_zero=True,
                             scale=True)
    elif ftype == 'ls':
        bands = np.array([0, wp/2, ws/2, fs/2])
        desired = [1, 1, 0, 0]
        weights = [1, 1]

        return signal.firls(Nmax, bands, desired, weights, fs=fs)

    # Use firpm
    N = bellangerord(0.001, 0.001, fs, ws-wp)
    N = min(Nmax, N)
    if N % 2 == 0:
        N = N + 1

    if ftype == 'firpm1f':
        return firpm1f(N, wp, ws, fs)
    elif ftype == 'firpm1f2':
        return firpm1f2(N, wp, ws, fs)
    else:
        raise ValueError('Unknown filter type {}'.format(ftype))

def lowpassIter(wp, ws, fs, f, atten=90, n_max=400):
    """Design a lowpass filter using f by iterating to minimize the number
    of taps needed.

    Args:
        wp: Passband frequency
        ws: Stopband frequency
        fs: Sample rate
        f: Function to design filter
        atten: desired attenuation (dB)
        n_max: Maximum semi-length of filter

    Returns:
        Filter taps.
    """
    n = bellangerord(0.01, 0.01, fs, (ws-wp))//2
    n_prev = 1
    n_lo = 1
    n_hi = None

    if n > n_max:
        n = n_max

    while n != n_prev:
        N = 2*n + 1

        taps = f(N, wp, ws, fs)

        w, h = signal.freqz(taps, worN=8000)
        w = 0.5*fs*w/np.pi
        hdb = 20*np.log10(np.abs(h))

        db = np.max(hdb[w >= ws])

        n_prev = n
        if db > -atten:
            if n == n_max:
                break

            n_lo = n
            if n_hi:
                n = (n_lo + n_hi) // 2
            else:
                n = 2*n
        else:
            n_hi = n
            n = (n_lo + n_hi) // 2

        if n > n_max:
            n = n_max

    return taps
