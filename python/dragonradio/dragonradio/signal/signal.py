# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import math

import numpy as np
from numpy.typing import ArrayLike

def dB2lin(dB: float) -> float:
    """Convert dB to linear (multiplicative) gain

    Args:
        dB (float): Gain in dB

    Returns:
        float: Linear gain
    """
    return 10.0**(dB/20.0)

def lin2dB(g: float) -> float:
    """Convert (multiplicative) gain to gain in dB

    Args:
        g (float): Linear gain

    Returns:
        float: Gain in dB
    """
    return 20.0*math.log(g)/math.log(10.0)

def mixUp(sig: ArrayLike, theta: float) -> np.ndarray:
    """Mix a signal up.

    Args:
        sig (np.ndarray): Signal to mix
        theta (float): Frequency to mix. Should be of the form 2*np.pi*shift.

    Returns:
        np.ndarray: Mixed signal.
    """
    return sig*np.exp(theta*1j*(np.arange(0,len(sig))))

def mixDown(sig: ArrayLike, theta: float) -> np.ndarray:
    """Mix a signal down.

    Args:
        sig (np.ndarray): Signal to mix
        theta (float): Frequency to mix. Should be of the form 2*np.pi*shift.

    Returns:
        np.ndarray: Mixed signal.
    """
    return sig*np.exp(-theta*1j*(np.arange(0,len(sig))))

def sigpow(sig: ArrayLike) -> float:
    """Compute signal power.

    Args:
        sig (np.ndarray): Signal

    Returns:
        float: Signal power
    """
    return np.mean(sig * np.conj(sig))

def tone(Fc: float, Fs: float, n: int) -> np.ndarray:
    """Generate a (complex) tone.

    Args:
        Fc (float): Center frequency of tone.
        Fs (float): Sampling rate.
        n (int): Number of samples to generate.

    Returns:
        np.ndarray: Tone signal (complex).
    """
    samples = np.arange(n) / Fs
    return np.exp(2 * np.pi * 1j * Fc * samples)

def chirp(f0: float, f1: float, Fs: float, n: int) -> np.ndarray:
    """Generate a (complex) chirp.

    Args:
        f0 (float): Initial frequency of tone.
        f1 (float): Final frequency of tone.
        Fs (float): Sampling rate.
        n (int): Number of samples to generate.

    Returns:
        np.ndarray: Chirp signal (complex).
    """
    T = n/Fs
    c = (f1 - f0)/T

    samples = np.arange(n) / Fs
    return np.exp(2 * np.pi * 1j * (c/2 * samples**2 + f0 * samples))
