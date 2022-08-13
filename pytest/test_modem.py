#! python
import numpy as np
from typing import Tuple
from unittest import TestCase

from hypothesis import given, strategies as st
from hypothesis.strategies import composite

from dragonradio.liquid import MCS, LiquidModulator, LiquidDemodulator
import dragonradio.liquid
from dragonradio.packet import Header

ModemPair = Tuple[LiquidModulator, LiquidDemodulator]
"""A modulator/demodulator pair"""

@composite
def ofdm_modem(draw, header_mcs: MCS) -> ModemPair:
    M = draw(st.sampled_from([48, 64, 128, 256]))
    cp_len = draw(st.sampled_from([6]))
    taper_len = draw(st.sampled_from([0, 4]))
    soft_header = draw(st.booleans())
    soft_payload = draw(st.booleans())

    mod = dragonradio.liquid.OFDMModulator(header_mcs, M, cp_len, taper_len)
    demod = dragonradio.liquid.OFDMDemodulator(header_mcs, soft_header, soft_payload, M, cp_len, taper_len)

    return (mod, demod)

@composite
def flexframe_modem(draw, header_mcs: MCS) -> ModemPair:
    soft_header = draw(st.booleans())
    soft_payload = draw(st.booleans())

    mod = dragonradio.liquid.FlexFrameModulator(header_mcs)
    demod = dragonradio.liquid.FlexFrameDemodulator(header_mcs, soft_header, soft_payload)

    return (mod, demod)

@composite
def newflexframe_modem(draw, header_mcs: MCS) -> ModemPair:
    soft_header = draw(st.booleans())
    soft_payload = draw(st.booleans())

    mod = dragonradio.liquid.NewFlexFrameModulator(header_mcs)
    demod = dragonradio.liquid.NewFlexFrameDemodulator(header_mcs, soft_header, soft_payload)

    return (mod, demod)

class TestModulation(TestCase):
    HEADER_MCS: MCS = MCS('crc32', 'secded7264', 'h84', 'bpsk')

    def run_modem(self,
                  hdr: Header,
                  payload_mcs: MCS,
                  payload: bytes,
                  mod: LiquidModulator,
                  demod: LiquidDemodulator):
        mod.payload_mcs = payload_mcs

        sig = mod.modulate(hdr, payload)

        n = 1
        pkts = demod.demodulate(np.concatenate(n*[sig]))

        self.assertEqual(len(pkts), 1)
        self.assertEqual(pkts[0][0], hdr)
        self.assertEqual(pkts[0][1], payload)

    @given(ms=st.sampled_from(['bpsk', 'qpsk', 'qam4', 'qam8', 'qam16', 'qam32', 'qam64', 'qam128', 'qam256']),
           payload=st.binary(min_size=1, max_size=1500),
           modem=st.one_of(ofdm_modem(HEADER_MCS), flexframe_modem(HEADER_MCS), newflexframe_modem(HEADER_MCS)))
    def test_modem(self, ms: str, payload: bytes, modem: ModemPair):
        mod, demod = modem

        self.run_modem(hdr=Header(2, 1, 0),
                       payload_mcs=MCS('crc32', 'rs8', 'none', ms),
                       payload=payload,
                       mod=mod,
                       demod=demod)
