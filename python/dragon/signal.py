import logging
import scipy.signal as signal

def lowpass(wp, ws, fs, atten=60):
    """Design a lowpass filter

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

    logging.debug("Creating prototype lowpass filter with %d taps, (wp=%g, ws=%g; fs=%g; beta = %f)",
        N, wp, ws, fs, beta)

    return signal.firwin(N, ws/2,
                         window=('kaiser', beta),
                         fs=fs,
                         pass_zero=True,
                         scale=True)
