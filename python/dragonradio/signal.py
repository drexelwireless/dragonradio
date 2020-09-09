import functools
import logging
import math
import numpy as np
import scipy.signal as signal

import dragonradio

def memoize(func):
    cache = {}

    @functools.wraps(func)
    def memoized_func(*args, **kwargs):
        key = str((args, kwargs))
        if key not in cache:
            cache[key] = func(*args, **kwargs)
        return cache[key]

    return memoized_func

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

def remez1f(numtaps, bands, desired, weight=None, Hz=None, type='bandpass', maxiter=25, grid_density=16, fs=None):
    """Calculate the minimax optimal filter using the Remez exchange algorithm,
    but with 1/f rolloff.

    See:
      https://dsp.stackexchange.com/questions/37709/convert-a-park-mcclellan-fir-solution-to-achieve-stop-band-roll-off
    """
    taps = signal.remez(numtaps, bands, desired, weight=weight, Hz=Hz, type=type, maxiter=maxiter, grid_density=grid_density, fs=fs)

    # Adjust to get roll-off.
    taps[0] = taps[1] / 2
    taps[numtaps-1] = taps[numtaps - 2]/2

    # Re-normalize taps
    return taps / np.sum(taps)

def firpm1f(N, wp, ws, fs):
    # Design a filter with 1/f^2 roll-off
    bands = np.array([0, wp/2, ws/2, fs/2])
    desired = [1, 1, 0, 0]
    weights = [1, 1]

    out = dragonradio.firpm1f(N, bands, desired, weights, fs=fs)

    return out.h

def firpm1f2(N, wp, ws, fs):
    # Design a filter with 1/f^2 roll-off
    bands = np.array([0, wp/2, ws/2, fs/2])
    desired = [1, 1, 0, 0]
    weights = [1, 1]

    out = dragonradio.firpm1f2(N, bands, desired, weights, fs=fs)

    return out.h

@memoize
def lowpass(wp, ws, fs):
    return lowpass_kaiser(wp, ws, fs, atten=60)

@memoize
def lowpass_firpm1f(wp, ws, fs, Nmax=301):
    # Use Bellanger's estimation of filter order
    N = bellangerord(0.001, 0.001, fs, ws-wp)
    N = min(Nmax, N)
    if N % 2 == 0:
        N = N + 1

    return firpm1f(N, wp, ws, fs)

@memoize
def lowpass_firpm1f2(wp, ws, fs, Nmax=301):
    # Use Bellanger's estimation of filter order
    N = bellangerord(0.001, 0.001, fs, ws-wp)
    N = min(Nmax, N)
    if N % 2 == 0:
        N = N + 1

    return firpm1f2(N, wp, ws, fs)

def lowpass_kaiser(wp, ws, fs, atten=60):
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

def lowpass_iter(wp, ws, fs, f, atten=90, n_max=400):
    """Design a lowpass filter using ftaps by iterating to minimize the number
    of taps needed.

    Args:
        wp: Passband frequency
        ws: Stopband frequency
        fs: Sample rate
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
