#! python
from datetime import timedelta
from typing import List, Tuple
from unittest import TestCase

import numpy as np
from numpy.typing import ArrayLike

from hypothesis import assume, example, given, settings, strategies as st

import dragonradio.signal
from dragonradio.signal import Resampler

resampler: st.SearchStrategy[Resampler] = st.sampled_from([dragonradio.signal.resample,
                                                           dragonradio.signal.resample_and_mix,
                                                           dragonradio.signal.fdresample])
"""A resampler"""

def rms(xs: ArrayLike):
   return np.sqrt(np.mean(xs * np.conj(xs)).real)

class TestResampling(TestCase):
    @given(sweep=st.sampled_from([(-480e3, 480e3)]),
           rate=st.sampled_from([1.0, 2.0, 3.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0]),
           resample=resampler)
    @settings(deadline=timedelta(seconds=10))
    def test_sweep(self, sweep: Tuple[float, float], rate: float, resample: Resampler):
        f0, f1 = sweep
        Fs = 1e6
        n = 100000

        sig1 = dragonradio.signal.chirp(f0, f1, Fs, n)
        sig2 = dragonradio.signal.chirp(f0, f1, Fs*rate, n*rate)
        resampled = dragonradio.signal.resample_and_filter(sig1, rate, 0.0, resample=resample, numtaps=701)

        n = min(len(sig2), len(resampled))

        err = rms(resampled[:n] - sig2[:n])

        self.assertLessEqual(err, 0.1)

    @given(Fc=st.sampled_from([-450e3, -300e3, -100e3, -50e3, 1e2, 1e3, 10e3, 100e3, 110e3, 300e3, 400e3, 450e3]),
           rate=st.sampled_from([1/2, 1/3, 2/3, 1/4, 3/4, 1/5, 2/5, 1/10, 1.0, 5/4, 3/2, 2.0, 3.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 50.0]),
           resample=resampler)
    @example(Fc=450e3,
             rate=50.0,
             resample=dragonradio.signal.fdresample)
    @settings(deadline=None)
    def test_tone(self, Fc: float, rate: float, resample: Resampler):
        Fs = 1e6
        n = 100000

        assume(abs(Fc) < 0.95*rate*Fs/2)

        sig1 = dragonradio.signal.tone(Fc, Fs, n)
        sig2 = dragonradio.signal.tone(Fc, Fs*rate, n*rate)
        resampled = dragonradio.signal.resample_and_filter(sig1, rate, 0.0, resample=resample, numtaps=1201)

        n = min(len(sig2), len(resampled))

        err = rms(resampled[:n] - sig2[:n])

        self.assertLessEqual(err, 0.1)
