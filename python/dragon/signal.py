import logging
import math
import numpy as np
import scipy.signal as signal

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

    logging.debug("Creating prototype lowpass filter with %d taps: wp=%g; ws=%g; fs=%g; beta = %f",
        N, wp, ws, fs, beta)

    return signal.firwin(N, ws/2,
                         window=('kaiser', beta),
                         fs=fs,
                         pass_zero=True,
                         scale=True)

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

def lowpass2(wp, ws, fs, atten=90, n_max=400):
    """Design a lowpass filter using a Parks-McClellan filter with 1/f rolloff.

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

    bands = np.array([0, wp, ws, fs/2])/(fs/2)
    print(n, bands)
    desired = [1, 1, 0, 0]
    weights = [1, 1]

    while n != n_prev:
        N = 2*n + 1

        #taps = signal.firls(N, bands, desired, weights, fs=2)
        taps = remez1f(N, bands, desired[::2], weights, fs=2)

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

    logging.debug("Creating prototype lowpass filter with %d taps: wp=%g; ws=%g; fs=%g",
        N, wp, ws, fs)

    return taps