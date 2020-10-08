# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import math

import numpy as np

def dB2gain(dB):
    """Convert dB to multiplicative gain"""
    return 10.0**(dB/20.0)

def gain2dB(g):
    """Convert multiplicative gain to dB"""
    return 20.0*math.log(g)/math.log(10.0)

def mixUp(sig: np.ndarray, theta: float):
    """Mix a signal up.

    Args:
        sig: Signal to mix
        theta: Frequency to mix. Should be of the form 2*np.pi*shift.
    """
    return sig*np.exp(theta*1j*(np.arange(0,len(sig))))

def mixDown(sig: np.ndarray, theta: float):
    """Mix a signal down.

    Args:
        sig: Signal to mix
        theta: Frequency to mix. Should be of the form 2*np.pi*shift.
    """
    return sig*np.exp(-theta*1j*(np.arange(0,len(sig))))
