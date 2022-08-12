#! python
import numpy as np
import random
from unittest import TestCase

from dragonradio.liquid import MCS, LiquidModulator, LiquidDemodulator
import dragonradio.liquid
from dragonradio.packet import Header

class TestModulation(TestCase):
    HEADER_MCS: MCS = MCS('crc32', 'secded7264', 'h84', 'bpsk')

    def run_modem(self, mod: LiquidModulator, demod: LiquidDemodulator, payload_size: int=1500):
        hdr = Header(2, 1, 0)
        payload_mcs = MCS('crc32', 'rs8', 'none', 'qam256')

        mod.payload_mcs = payload_mcs

        # Generate random payload
        payload = bytearray(random.getrandbits(8) for _ in range(payload_size))

        sig = mod.modulate(hdr, payload)

        n = 1
        pkts = demod.demodulate(np.concatenate(n*[sig]))

        self.assertEqual(len(pkts), 1)
        self.assertEqual(pkts[0][0], hdr)
        self.assertEqual(pkts[0][1], payload)

    def test_ofdm(self, M: int=48, cp_len: int=6, taper_len: int=4):
        mod = dragonradio.liquid.OFDMModulator(self.HEADER_MCS, M, cp_len, taper_len)
        demod = dragonradio.liquid.OFDMDemodulator(self.HEADER_MCS, True, False, M, cp_len, taper_len)

        self.run_modem(mod, demod)

    def test_flexframe(self):
        mod = dragonradio.liquid.FlexFrameModulator(self.HEADER_MCS)
        demod = dragonradio.liquid.FlexFrameDemodulator(self.HEADER_MCS, True, False)

        self.run_modem(mod, demod)

    def test_newflexframe(self):
        mod = dragonradio.liquid.NewFlexFrameModulator(self.HEADER_MCS)
        demod = dragonradio.liquid.NewFlexFrameDemodulator(self.HEADER_MCS, True, False)

        self.run_modem(mod, demod)
