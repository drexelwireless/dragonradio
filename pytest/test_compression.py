import numpy as np
from unittest import TestCase

import dragonradio.signal

def complex64_strategy(n: int):
    i = np.random.random_sample(n)
    q = np.random.random_sample(n)

    iq = i + q*1j
    iq /= np.max(np.abs(iq))

    return iq.astype(np.complex64)

class TestCompression(TestCase):
    def test_sc16(self):
        sig = complex64_strategy(1024)
        sig_fc32 = sig.astype(np.complex64)

        sig_sc16 = dragonradio.signal.convert2sc16(sig_fc32)
        sig_fc32_2 = dragonradio.signal.convert2fc32(sig_sc16)

        self.assertEqual(len(sig_fc32_2), len(sig_fc32))
        self.assertLess(max(abs(sig_fc32_2 - sig_fc32)), 1e-4)

        print("sc16 conversion savings:", 1.0 - (2.0*len(sig_sc16))/(8.0*len(sig_fc32)))

        return True

    def test_compression(self):
        sig = complex64_strategy(1024)
        sig_fc32 = sig.astype(np.complex64)

        data = dragonradio.signal.compressIQData(sig_fc32)
        sig_fc32_2 = dragonradio.signal.decompressIQData(data)

        self.assertEqual(len(sig_fc32_2), len(sig_fc32))
        self.assertLess(max(abs(sig_fc32_2 - sig_fc32)), 2e-4)

        print("FLAC compression savings:", 1.0 - len(data)/(8.0*len(sig_fc32)))

        return True
