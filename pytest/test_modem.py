#! python
from datetime import timedelta
from fractions import Fraction
import sys
from typing import Optional, Tuple
from unittest import TestCase

import numpy as np
from numpy.typing import ArrayLike

import pytest

from hypothesis import given, note, settings, strategies as st
from hypothesis.strategies import composite

from dragonradio.liquid import MCS, LiquidModulator, LiquidDemodulator
import dragonradio.liquid
from dragonradio.packet import Header
from dragonradio.signal import Resampler
import dragonradio.signal.pyresample as pyresample

try:
    import dragonradio.tools.modem as modem
except:
    pass

resampler: st.SearchStrategy[Resampler] = st.sampled_from([dragonradio.signal.resample,
                                                           dragonradio.signal.resample_and_mix,
                                                           pyresample.fdresample])
"""A resampler"""

Channel = Tuple[float, float, float]

@composite
def channel(draw) -> Channel:
    fs = draw(st.sampled_from([1e6, 2e6, 3e6, 5e6, 10e6, 15e6, 20e6, 25e6]))
    cbw = draw(st.sampled_from([100e3, 250e3, 300e3, 1e6, 2e6, 3e6, 5e6]).filter(lambda cbw: cbw < fs and fs/cbw <= 25))
    fc = draw(st.sampled_from([1e6*x for x in  range(0, 25)]).filter(lambda fc: fc + cbw/2 < fs))
    sign = draw(st.booleans())

    if sign:
        fc = -fc

    return (cbw, fc, fs)

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

    def resample_and_filter(self, sig: ArrayLike, rate: float, theta: float, resample: Resampler):
        return dragonradio.signal.resample_and_filter(sig, rate, theta, resample, numtaps=1201)

    def run_modem(self,
                  hdr: Header,
                  payload_mcs: MCS,
                  payload: bytes,
                  mod: LiquidModulator,
                  demod: LiquidDemodulator,
                  cbw: float=1e6,
                  Fc: float=0,
                  Fs: float=1e6,
                  upsample: Optional[Resampler]=None,
                  downsample: Optional[Resampler]=None):
        # Set payload MCS
        mod.payload_mcs = payload_mcs

        # Modulate signal
        sig = mod.modulate(hdr, payload)

        # Create multiples copies of signal
        n = 1
        sig = np.concatenate(n*[sig])

        if cbw != Fs:
            # Upsample
            upsampled = self.resample_and_filter(sig, Fs/cbw, Fc/Fs, upsample)

            # Downsample
            downsampled = self.resample_and_filter(upsampled, cbw/Fs, Fc/Fs, downsample)

            sig = downsampled

        # Demodulate signal
        pkts = demod.demodulate(sig)

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

    @given(modem=ofdm_modem(HEADER_MCS),
           channel=channel(),
           ms=st.sampled_from(['bpsk', 'qpsk', 'qam4', 'qam8', 'qam16', 'qam32', 'qam64', 'qam128', 'qam256']),
           payload=st.binary(max_size=1500),
           upsample=resampler,
           downsample=resampler)
    @settings(deadline=None)
    def test_ofdm_resampled(self, modem: ModemPair, channel: Channel, ms: str, payload: bytes, upsample: Resampler, downsample: Resampler):
        mod, demod = modem
        (cbw, Fc, Fs) = channel

        rate = Fraction(cbw/Fs).limit_denominator(200)
        note(f"Rate: {rate:}")

        return self.run_modem(cbw=cbw, Fc=Fc, Fs=Fs,
                              hdr=Header(2, 1, 0),
                              payload_mcs=MCS('crc32', 'rs8', 'none', ms),
                              payload=payload,
                              mod=mod,
                              demod=demod,
                              upsample=upsample,
                              downsample=downsample)

    @pytest.mark.skipif('dragonradio.tools.modem' not in sys.modules,
                        reason="requires the dragonradio.tools.modem library")
    @given(Fc=st.sampled_from([1e6, 2e6, 3e6, -4e6]),
           payload=st.binary(max_size=1500),
           ms=st.sampled_from(['bpsk', 'qpsk', 'qam4', 'qam8', 'qam16', 'qam32', 'qam64', 'qam128', 'qam256']))
    @settings(deadline=timedelta(seconds=1))
    def test_modem_module(self, Fc: float, ms: str, payload: bytes):
        # Sampling frequency
        Fs = 10e6

        # Center frequency
        Fc = 1e6

        # Channel bandwidth
        cbw = 1e6

        # Modulate first packet
        hdr = dragonradio.packet.Header(1, 2, 0)
        payload_mcs = dragonradio.liquid.MCS('crc32', 'none', 'v27', ms)

        sig = modem.modulate(hdr, payload_mcs, payload, cbw, Fc, Fs)

        pkts = modem.demodulate(sig, cbw, Fc, Fs)

        self.assertEqual(len(pkts), 1)
        self.assertEqual(pkts[0][0], hdr)
        self.assertEqual(pkts[0][1], payload)
