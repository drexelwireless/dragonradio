from __future__ import annotations
import dragonradiocpp.packet
import dragonradiocpp.radio
import numpy
import typing
__all__ = ['CRCScheme', 'FECScheme', 'FlexFrame', 'FlexFrameDemodulator', 'FlexFrameModulator', 'FrameStats', 'LiquidDemodulator', 'LiquidModulator', 'LiquidPHY', 'MCS', 'ModulationScheme', 'NewFlexFrame', 'NewFlexFrameDemodulator', 'NewFlexFrameModulator', 'OFDM', 'OFDMDemodulator', 'OFDMModulator', 'V29', 'apsk128', 'apsk16', 'apsk256', 'apsk32', 'apsk4', 'apsk64', 'apsk8', 'arb', 'arb128opt', 'arb16opt', 'arb256opt', 'arb32opt', 'arb64opt', 'arb64vt', 'ask128', 'ask16', 'ask2', 'ask256', 'ask32', 'ask4', 'ask64', 'ask8', 'bpsk', 'checksum', 'crc16', 'crc24', 'crc32', 'crc8', 'dpsk128', 'dpsk16', 'dpsk2', 'dpsk256', 'dpsk32', 'dpsk4', 'dpsk64', 'dpsk8', 'g2412', 'h128', 'h74', 'h84', 'kSCTypeData', 'kSCTypeNull', 'kSCTypePilot', 'none', 'ook', 'psk128', 'psk16', 'psk2', 'psk256', 'psk32', 'psk4', 'psk64', 'psk8', 'qam128', 'qam16', 'qam256', 'qam32', 'qam4', 'qam64', 'qam8', 'qpsk', 'rep3', 'rep5', 'rs8', 'secded2216', 'secded3932', 'secded7264', 'sqam128', 'sqam32', 'unknown', 'v27', 'v27p23', 'v27p34', 'v27p45', 'v27p56', 'v27p67', 'v27p78', 'v29', 'v29p23', 'v29p34', 'v29p45', 'v29p56', 'v29p67', 'v29p78', 'v39', 'v615']
class CRCScheme:
    """
    A liquid CRC scheme
    
    Members:
    
      unknown : unknown
    
      none : none
    
      checksum : checksum (8-bit)
    
      crc8 : CRC (8-bit)
    
      crc16 : CRC (16-bit)
    
      crc24 : CRC (24-bit)
    
      crc32 : CRC (32-bit)
    """
    __members__: typing.ClassVar[dict[str, CRCScheme]]  # value = {'unknown': <CRCScheme.unknown: 0>, 'none': <CRCScheme.none: 1>, 'checksum': <CRCScheme.checksum: 2>, 'crc8': <CRCScheme.crc8: 3>, 'crc16': <CRCScheme.crc16: 4>, 'crc24': <CRCScheme.crc24: 5>, 'crc32': <CRCScheme.crc32: 6>}
    checksum: typing.ClassVar[CRCScheme]  # value = <CRCScheme.checksum: 2>
    crc16: typing.ClassVar[CRCScheme]  # value = <CRCScheme.crc16: 4>
    crc24: typing.ClassVar[CRCScheme]  # value = <CRCScheme.crc24: 5>
    crc32: typing.ClassVar[CRCScheme]  # value = <CRCScheme.crc32: 6>
    crc8: typing.ClassVar[CRCScheme]  # value = <CRCScheme.crc8: 3>
    none: typing.ClassVar[CRCScheme]  # value = <CRCScheme.none: 1>
    unknown: typing.ClassVar[CRCScheme]  # value = <CRCScheme.unknown: 0>
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    @typing.overload
    def __init__(self, value: int) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: str) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class FECScheme:
    """
    A liquid FEC scheme
    
    Members:
    
      unknown : unknown
    
      none : none
    
      rep3 : repeat(3)
    
      rep5 : repeat(5)
    
      h74 : Hamming(7,4)
    
      h84 : Hamming(8,4)
    
      h128 : Hamming(12,8)
    
      g2412 : Golay(24,12)
    
      secded2216 : SEC-DEC(22,16)
    
      secded3932 : SEC-DEC(39,32)
    
      secded7264 : SEC-DEC(72,64)
    
      v27 : convolutional r1/2 K=7
    
      v29 : convolutional r1/2 K=9
    
      v39 : convolutional r1/3 K=9
    
      v615 : convolutional r1/6 K=15
    
      v27p23 : convolutional r2/3 K=7 (punctured)
    
      v27p34 : convolutional r3/4 K=7 (punctured)
    
      v27p45 : convolutional r4/5 K=7 (punctured)
    
      v27p56 : convolutional r5/6 K=7 (punctured)
    
      v27p67 : convolutional r6/7 K=7 (punctured)
    
      v27p78 : convolutional r7/8 K=7 (punctured)
    
      v29p23 : convolutional r2/3 K=9 (punctured)
    
      v29p34 : convolutional r3/4 K=9 (punctured)
    
      v29p45 : convolutional r4/5 K=9 (punctured)
    
      v29p56 : convolutional r5/6 K=9 (punctured)
    
      v29p67 : convolutional r6/7 K=9 (punctured)
    
      v29p78 : convolutional r7/8 K=9 (punctured)
    
      rs8 : Reed-Solomon, 223/255
    """
    __members__: typing.ClassVar[dict[str, FECScheme]]  # value = {'unknown': <FECScheme.unknown: 0>, 'none': <FECScheme.none: 1>, 'rep3': <FECScheme.rep3: 2>, 'rep5': <FECScheme.rep5: 3>, 'h74': <FECScheme.h74: 4>, 'h84': <FECScheme.h84: 5>, 'h128': <FECScheme.h128: 6>, 'g2412': <FECScheme.g2412: 7>, 'secded2216': <FECScheme.secded2216: 8>, 'secded3932': <FECScheme.secded3932: 9>, 'secded7264': <FECScheme.secded7264: 10>, 'v27': <FECScheme.v27: 11>, 'v29': <FECScheme.v29: 12>, 'v39': <FECScheme.v39: 13>, 'v615': <FECScheme.v615: 14>, 'v27p23': <FECScheme.v27p23: 15>, 'v27p34': <FECScheme.v27p34: 16>, 'v27p45': <FECScheme.v27p45: 17>, 'v27p56': <FECScheme.v27p56: 18>, 'v27p67': <FECScheme.v27p67: 19>, 'v27p78': <FECScheme.v27p78: 20>, 'v29p23': <FECScheme.v29p23: 21>, 'v29p34': <FECScheme.v29p34: 22>, 'v29p45': <FECScheme.v29p45: 23>, 'v29p56': <FECScheme.v29p56: 24>, 'v29p67': <FECScheme.v29p67: 25>, 'v29p78': <FECScheme.v29p78: 26>, 'rs8': <FECScheme.rs8: 27>}
    g2412: typing.ClassVar[FECScheme]  # value = <FECScheme.g2412: 7>
    h128: typing.ClassVar[FECScheme]  # value = <FECScheme.h128: 6>
    h74: typing.ClassVar[FECScheme]  # value = <FECScheme.h74: 4>
    h84: typing.ClassVar[FECScheme]  # value = <FECScheme.h84: 5>
    none: typing.ClassVar[FECScheme]  # value = <FECScheme.none: 1>
    rep3: typing.ClassVar[FECScheme]  # value = <FECScheme.rep3: 2>
    rep5: typing.ClassVar[FECScheme]  # value = <FECScheme.rep5: 3>
    rs8: typing.ClassVar[FECScheme]  # value = <FECScheme.rs8: 27>
    secded2216: typing.ClassVar[FECScheme]  # value = <FECScheme.secded2216: 8>
    secded3932: typing.ClassVar[FECScheme]  # value = <FECScheme.secded3932: 9>
    secded7264: typing.ClassVar[FECScheme]  # value = <FECScheme.secded7264: 10>
    unknown: typing.ClassVar[FECScheme]  # value = <FECScheme.unknown: 0>
    v27: typing.ClassVar[FECScheme]  # value = <FECScheme.v27: 11>
    v27p23: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p23: 15>
    v27p34: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p34: 16>
    v27p45: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p45: 17>
    v27p56: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p56: 18>
    v27p67: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p67: 19>
    v27p78: typing.ClassVar[FECScheme]  # value = <FECScheme.v27p78: 20>
    v29: typing.ClassVar[FECScheme]  # value = <FECScheme.v29: 12>
    v29p23: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p23: 21>
    v29p34: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p34: 22>
    v29p45: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p45: 23>
    v29p56: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p56: 24>
    v29p67: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p67: 25>
    v29p78: typing.ClassVar[FECScheme]  # value = <FECScheme.v29p78: 26>
    v39: typing.ClassVar[FECScheme]  # value = <FECScheme.v39: 13>
    v615: typing.ClassVar[FECScheme]  # value = <FECScheme.v615: 14>
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    @typing.overload
    def __init__(self, value: int) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: str) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class FlexFrame(LiquidPHY):
    """
    Liquid FlexFrame PHY
    """
    def __init__(self, header_mcs: MCS, mcs_table: list[tuple[MCS, dragonradiocpp.radio.AutoGain]], soft_header: bool, soft_payload: bool) -> None:
        """
        Initialize FlexFrame PHY
        """
class FlexFrameDemodulator(LiquidDemodulator):
    """
    A liquid FlexFrame demodulator
    """
    def __init__(self, arg0: MCS, arg1: bool, arg2: bool) -> None:
        ...
class FlexFrameModulator(LiquidModulator):
    """
    A liquid FlexFrame modulator
    """
    def __init__(self, arg0: MCS) -> None:
        ...
class FrameStats:
    """
    Frame statistics
    """
    def __init__(self) -> None:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def cfo(self) -> float:
        """
        float: Carrier Frequency Offset (f/Fs)
        """
    @property
    def check(self) -> CRCScheme:
        """
        CRCScheme: Data validity check (crc, checksum)
        """
    @property
    def end_counter(self) -> int:
        """
        int: Sample offset of end of demodulated packet
        """
    @property
    def evm(self) -> float:
        """
        float: Error Vector Magnitude (dB)
        """
    @property
    def fec0(self) -> FECScheme:
        """
        FECScheme: Forward Error-Correction (inner)
        """
    @property
    def fec1(self) -> FECScheme:
        """
        FECScheme: Forward Error-Correction (outer)
        """
    @property
    def mod_bps(self) -> int:
        """
        int: Modulation depth (bits/symbol)
        """
    @property
    def mod_scheme(self) -> ModulationScheme:
        """
        ModulationScheme: Modulation scheme
        """
    @property
    def rssi(self) -> float:
        """
        float: Received Signal Strength Indicator (dB)
        """
    @property
    def start_counter(self) -> int:
        """
        int: Sample offset of start of demodulated packet
        """
class LiquidDemodulator(dragonradiocpp.radio.Demodulator):
    """
    A liquid demodulator
    """
    def demodulate(self, buf: numpy.ndarray) -> list[tuple[dragonradiocpp.packet.Header | None, bytes | None, FrameStats]]:
        """
        Demodulate a signal
        """
    @property
    def header_mcs(self) -> MCS:
        """
        MCS: Header MCS
        """
    @header_mcs.setter
    def header_mcs(self, arg1: MCS) -> None:
        ...
    @property
    def soft_header(self) -> bool:
        """
        bool: Use soft decoding for header
        """
    @property
    def soft_payload(self) -> bool:
        """
        bool: Use soft decoding for payload
        """
class LiquidModulator(dragonradiocpp.radio.Modulator):
    """
    A liquid modulator
    """
    @property
    def header_mcs(self) -> MCS:
        """
        MCS: Header MCS
        """
    @header_mcs.setter
    def header_mcs(self, arg1: MCS) -> None:
        ...
    @property
    def payload_mcs(self) -> MCS:
        """
        MCS: Payload MCS
        """
    @payload_mcs.setter
    def payload_mcs(self, arg1: MCS) -> None:
        ...
class LiquidPHY(dragonradiocpp.radio.PHY):
    @property
    def header_mcs(self) -> MCS:
        """
        MCS: Header MCS
        """
    @property
    def soft_header(self) -> bool:
        """
        bool: Use soft decoding for header
        """
    @property
    def soft_payload(self) -> bool:
        """
        bool: Use soft decoding for payload
        """
class MCS(dragonradiocpp.radio.MCS):
    """
    A liquid modulation and coding scheme
    """
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, check: CRCScheme, fec0: FECScheme, fec1: FECScheme, ms: ModulationScheme) -> None:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def check(self) -> CRCScheme:
        """
        CRCScheme: Data validity check
        """
    @check.setter
    def check(self, arg0: CRCScheme) -> None:
        ...
    @property
    def fec0(self) -> FECScheme:
        """
        FECScheme: Inner FEC
        """
    @fec0.setter
    def fec0(self, arg0: FECScheme) -> None:
        ...
    @property
    def fec1(self) -> FECScheme:
        """
        FECScheme: Outer FEC
        """
    @fec1.setter
    def fec1(self, arg0: FECScheme) -> None:
        ...
    @property
    def ms(self) -> ModulationScheme:
        """
        ModulationScheme: Modulation scheme
        """
    @ms.setter
    def ms(self, arg0: ModulationScheme) -> None:
        ...
class ModulationScheme:
    """
    A liquid modulation scheme
    
    Members:
    
      unknown : unkown
    
      psk2 : phase-shift keying (2)
    
      psk4 : phase-shift keying (4)
    
      psk8 : phase-shift keying (8)
    
      psk16 : phase-shift keying (16)
    
      psk32 : phase-shift keying (32)
    
      psk64 : phase-shift keying (64)
    
      psk128 : phase-shift keying (128)
    
      psk256 : phase-shift keying (256)
    
      dpsk2 : differential phase-shift keying (2)
    
      dpsk4 : differential phase-shift keying (4)
    
      dpsk8 : differential phase-shift keying (8)
    
      dpsk16 : differential phase-shift keying (16)
    
      dpsk32 : differential phase-shift keying (32)
    
      dpsk64 : differential phase-shift keying (64)
    
      dpsk128 : differential phase-shift keying (128)
    
      dpsk256 : differential phase-shift keying (256)
    
      ask2 : amplitude-shift keying (2)
    
      ask4 : amplitude-shift keying (4)
    
      ask8 : amplitude-shift keying (8)
    
      ask16 : amplitude-shift keying (16)
    
      ask32 : amplitude-shift keying (32)
    
      ask64 : amplitude-shift keying (64)
    
      ask128 : amplitude-shift keying (128)
    
      ask256 : amplitude-shift keying (256)
    
      qam4 : quadrature amplitude-shift keying (4)
    
      qam8 : quadrature amplitude-shift keying (8)
    
      qam16 : quadrature amplitude-shift keying (16)
    
      qam32 : quadrature amplitude-shift keying (32)
    
      qam64 : quadrature amplitude-shift keying (64)
    
      qam128 : quadrature amplitude-shift keying (128)
    
      qam256 : quadrature amplitude-shift keying (256)
    
      apsk4 : amplitude/phase-shift keying (4)
    
      apsk8 : amplitude/phase-shift keying (8)
    
      apsk16 : amplitude/phase-shift keying (16)
    
      apsk32 : amplitude/phase-shift keying (32)
    
      apsk64 : amplitude/phase-shift keying (64)
    
      apsk128 : amplitude/phase-shift keying (128)
    
      apsk256 : amplitude/phase-shift keying (256)
    
      bpsk : binary phase-shift keying
    
      qpsk : quaternary phase-shift keying
    
      ook : ook (on/off keying)
    
      sqam32 : 'square' 32-QAM
    
      sqam128 : 'square' 128-QAM
    
      V29 : V.29
    
      arb16opt : arb16opt (optimal 16-qam)
    
      arb32opt : arb32opt (optimal 32-qam)
    
      arb64opt : arb64opt (optimal 64-qam)
    
      arb128opt : arb128opt (optimal 128-qam)
    
      arb256opt : arb256opt (optimal 256-qam)
    
      arb64vt : arb64vt (64-qam vt logo)
    
      arb : arbitrary constellation
    """
    V29: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.V29: 44>
    __members__: typing.ClassVar[dict[str, ModulationScheme]]  # value = {'unknown': <ModulationScheme.unknown: 0>, 'psk2': <ModulationScheme.psk2: 1>, 'psk4': <ModulationScheme.psk4: 2>, 'psk8': <ModulationScheme.psk8: 3>, 'psk16': <ModulationScheme.psk16: 4>, 'psk32': <ModulationScheme.psk32: 5>, 'psk64': <ModulationScheme.psk64: 6>, 'psk128': <ModulationScheme.psk128: 7>, 'psk256': <ModulationScheme.psk256: 8>, 'dpsk2': <ModulationScheme.dpsk2: 9>, 'dpsk4': <ModulationScheme.dpsk4: 10>, 'dpsk8': <ModulationScheme.dpsk8: 11>, 'dpsk16': <ModulationScheme.dpsk16: 12>, 'dpsk32': <ModulationScheme.dpsk32: 13>, 'dpsk64': <ModulationScheme.dpsk64: 14>, 'dpsk128': <ModulationScheme.dpsk128: 15>, 'dpsk256': <ModulationScheme.dpsk256: 16>, 'ask2': <ModulationScheme.ask2: 17>, 'ask4': <ModulationScheme.ask4: 18>, 'ask8': <ModulationScheme.ask8: 19>, 'ask16': <ModulationScheme.ask16: 20>, 'ask32': <ModulationScheme.ask32: 21>, 'ask64': <ModulationScheme.ask64: 22>, 'ask128': <ModulationScheme.ask128: 23>, 'ask256': <ModulationScheme.ask256: 24>, 'qam4': <ModulationScheme.qam4: 25>, 'qam8': <ModulationScheme.qam8: 26>, 'qam16': <ModulationScheme.qam16: 27>, 'qam32': <ModulationScheme.qam32: 28>, 'qam64': <ModulationScheme.qam64: 29>, 'qam128': <ModulationScheme.qam128: 30>, 'qam256': <ModulationScheme.qam256: 31>, 'apsk4': <ModulationScheme.apsk4: 32>, 'apsk8': <ModulationScheme.apsk8: 33>, 'apsk16': <ModulationScheme.apsk16: 34>, 'apsk32': <ModulationScheme.apsk32: 35>, 'apsk64': <ModulationScheme.apsk64: 36>, 'apsk128': <ModulationScheme.apsk128: 37>, 'apsk256': <ModulationScheme.apsk256: 38>, 'bpsk': <ModulationScheme.bpsk: 39>, 'qpsk': <ModulationScheme.qpsk: 40>, 'ook': <ModulationScheme.ook: 41>, 'sqam32': <ModulationScheme.sqam32: 42>, 'sqam128': <ModulationScheme.sqam128: 43>, 'V29': <ModulationScheme.V29: 44>, 'arb16opt': <ModulationScheme.arb16opt: 45>, 'arb32opt': <ModulationScheme.arb32opt: 46>, 'arb64opt': <ModulationScheme.arb64opt: 47>, 'arb128opt': <ModulationScheme.arb128opt: 48>, 'arb256opt': <ModulationScheme.arb256opt: 49>, 'arb64vt': <ModulationScheme.arb64vt: 50>, 'arb': <ModulationScheme.arb: 51>}
    apsk128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk128: 37>
    apsk16: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk16: 34>
    apsk256: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk256: 38>
    apsk32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk32: 35>
    apsk4: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk4: 32>
    apsk64: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk64: 36>
    apsk8: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.apsk8: 33>
    arb: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb: 51>
    arb128opt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb128opt: 48>
    arb16opt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb16opt: 45>
    arb256opt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb256opt: 49>
    arb32opt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb32opt: 46>
    arb64opt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb64opt: 47>
    arb64vt: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.arb64vt: 50>
    ask128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask128: 23>
    ask16: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask16: 20>
    ask2: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask2: 17>
    ask256: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask256: 24>
    ask32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask32: 21>
    ask4: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask4: 18>
    ask64: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask64: 22>
    ask8: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ask8: 19>
    bpsk: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.bpsk: 39>
    dpsk128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk128: 15>
    dpsk16: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk16: 12>
    dpsk2: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk2: 9>
    dpsk256: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk256: 16>
    dpsk32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk32: 13>
    dpsk4: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk4: 10>
    dpsk64: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk64: 14>
    dpsk8: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.dpsk8: 11>
    ook: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.ook: 41>
    psk128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk128: 7>
    psk16: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk16: 4>
    psk2: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk2: 1>
    psk256: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk256: 8>
    psk32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk32: 5>
    psk4: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk4: 2>
    psk64: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk64: 6>
    psk8: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.psk8: 3>
    qam128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam128: 30>
    qam16: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam16: 27>
    qam256: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam256: 31>
    qam32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam32: 28>
    qam4: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam4: 25>
    qam64: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam64: 29>
    qam8: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qam8: 26>
    qpsk: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.qpsk: 40>
    sqam128: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.sqam128: 43>
    sqam32: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.sqam32: 42>
    unknown: typing.ClassVar[ModulationScheme]  # value = <ModulationScheme.unknown: 0>
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    @typing.overload
    def __init__(self, value: int) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: str) -> None:
        ...
    def __int__(self) -> int:
        ...
    def __ne__(self, other: typing.Any) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, state: int) -> None:
        ...
    def __str__(self) -> str:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def value(self) -> int:
        ...
class NewFlexFrame(LiquidPHY):
    """
    Liquid "new" FlexFrame PHY
    """
    def __init__(self, header_mcs: MCS, mcs_table: list[tuple[MCS, dragonradiocpp.radio.AutoGain]], soft_header: bool, soft_payload: bool) -> None:
        """
        Initialize "new" FlexFrame PHY
        """
class NewFlexFrameDemodulator(LiquidDemodulator):
    """
    A liquid "new" FlexFrame demodulator
    """
    def __init__(self, arg0: MCS, arg1: bool, arg2: bool) -> None:
        ...
class NewFlexFrameModulator(LiquidModulator):
    """
    A liquid "new" FlexFrame modulator
    """
    def __init__(self, arg0: MCS) -> None:
        ...
class OFDM(LiquidPHY):
    """
    Liquid OFDM PHY
    """
    def __init__(self, header_mcs: MCS, mcs_table: list[tuple[MCS, dragonradiocpp.radio.AutoGain]], soft_header: bool, soft_payload: bool, M: int, cp_len: int, taper_len: int, subcarriers: str | None) -> None:
        """
        Initialize OFDM PHY
        """
    @property
    def subcarriers(self) -> str:
        """
        str: Subcarriers
        """
class OFDMDemodulator(LiquidDemodulator):
    """
    A liquid OFDM demodulator
    """
    @typing.overload
    def __init__(self, header_mcs: MCS, soft_header: bool, soft_payload: bool, M: int, cp_len: int, taper_len: int) -> None:
        """
        Construct an OFDM demodulator
        """
    @typing.overload
    def __init__(self, header_mcs: MCS, soft_header: bool, soft_payload: bool, M: int, cp_len: int, taper_len: int, subcarriers: str | None) -> None:
        """
        Construct an OFDM demodulator
        """
class OFDMModulator(LiquidModulator):
    """
    A liquid OFDM modulator
    """
    @typing.overload
    def __init__(self, header_mcs: MCS, M: int, cp_len: int, taper_len: int) -> None:
        """
        Construct an OFDM modulator
        """
    @typing.overload
    def __init__(self, header_mcs: MCS, M: int, cp_len: int, taper_len: int, subcarriers: str | None) -> None:
        """
        Construct an OFDM modulator
        """
V29: ModulationScheme  # value = <ModulationScheme.V29: 44>
apsk128: ModulationScheme  # value = <ModulationScheme.apsk128: 37>
apsk16: ModulationScheme  # value = <ModulationScheme.apsk16: 34>
apsk256: ModulationScheme  # value = <ModulationScheme.apsk256: 38>
apsk32: ModulationScheme  # value = <ModulationScheme.apsk32: 35>
apsk4: ModulationScheme  # value = <ModulationScheme.apsk4: 32>
apsk64: ModulationScheme  # value = <ModulationScheme.apsk64: 36>
apsk8: ModulationScheme  # value = <ModulationScheme.apsk8: 33>
arb: ModulationScheme  # value = <ModulationScheme.arb: 51>
arb128opt: ModulationScheme  # value = <ModulationScheme.arb128opt: 48>
arb16opt: ModulationScheme  # value = <ModulationScheme.arb16opt: 45>
arb256opt: ModulationScheme  # value = <ModulationScheme.arb256opt: 49>
arb32opt: ModulationScheme  # value = <ModulationScheme.arb32opt: 46>
arb64opt: ModulationScheme  # value = <ModulationScheme.arb64opt: 47>
arb64vt: ModulationScheme  # value = <ModulationScheme.arb64vt: 50>
ask128: ModulationScheme  # value = <ModulationScheme.ask128: 23>
ask16: ModulationScheme  # value = <ModulationScheme.ask16: 20>
ask2: ModulationScheme  # value = <ModulationScheme.ask2: 17>
ask256: ModulationScheme  # value = <ModulationScheme.ask256: 24>
ask32: ModulationScheme  # value = <ModulationScheme.ask32: 21>
ask4: ModulationScheme  # value = <ModulationScheme.ask4: 18>
ask64: ModulationScheme  # value = <ModulationScheme.ask64: 22>
ask8: ModulationScheme  # value = <ModulationScheme.ask8: 19>
bpsk: ModulationScheme  # value = <ModulationScheme.bpsk: 39>
checksum: CRCScheme  # value = <CRCScheme.checksum: 2>
crc16: CRCScheme  # value = <CRCScheme.crc16: 4>
crc24: CRCScheme  # value = <CRCScheme.crc24: 5>
crc32: CRCScheme  # value = <CRCScheme.crc32: 6>
crc8: CRCScheme  # value = <CRCScheme.crc8: 3>
dpsk128: ModulationScheme  # value = <ModulationScheme.dpsk128: 15>
dpsk16: ModulationScheme  # value = <ModulationScheme.dpsk16: 12>
dpsk2: ModulationScheme  # value = <ModulationScheme.dpsk2: 9>
dpsk256: ModulationScheme  # value = <ModulationScheme.dpsk256: 16>
dpsk32: ModulationScheme  # value = <ModulationScheme.dpsk32: 13>
dpsk4: ModulationScheme  # value = <ModulationScheme.dpsk4: 10>
dpsk64: ModulationScheme  # value = <ModulationScheme.dpsk64: 14>
dpsk8: ModulationScheme  # value = <ModulationScheme.dpsk8: 11>
g2412: FECScheme  # value = <FECScheme.g2412: 7>
h128: FECScheme  # value = <FECScheme.h128: 6>
h74: FECScheme  # value = <FECScheme.h74: 4>
h84: FECScheme  # value = <FECScheme.h84: 5>
kSCTypeData: int = 2
kSCTypeNull: int = 0
kSCTypePilot: int = 1
none: FECScheme  # value = <FECScheme.none: 1>
ook: ModulationScheme  # value = <ModulationScheme.ook: 41>
psk128: ModulationScheme  # value = <ModulationScheme.psk128: 7>
psk16: ModulationScheme  # value = <ModulationScheme.psk16: 4>
psk2: ModulationScheme  # value = <ModulationScheme.psk2: 1>
psk256: ModulationScheme  # value = <ModulationScheme.psk256: 8>
psk32: ModulationScheme  # value = <ModulationScheme.psk32: 5>
psk4: ModulationScheme  # value = <ModulationScheme.psk4: 2>
psk64: ModulationScheme  # value = <ModulationScheme.psk64: 6>
psk8: ModulationScheme  # value = <ModulationScheme.psk8: 3>
qam128: ModulationScheme  # value = <ModulationScheme.qam128: 30>
qam16: ModulationScheme  # value = <ModulationScheme.qam16: 27>
qam256: ModulationScheme  # value = <ModulationScheme.qam256: 31>
qam32: ModulationScheme  # value = <ModulationScheme.qam32: 28>
qam4: ModulationScheme  # value = <ModulationScheme.qam4: 25>
qam64: ModulationScheme  # value = <ModulationScheme.qam64: 29>
qam8: ModulationScheme  # value = <ModulationScheme.qam8: 26>
qpsk: ModulationScheme  # value = <ModulationScheme.qpsk: 40>
rep3: FECScheme  # value = <FECScheme.rep3: 2>
rep5: FECScheme  # value = <FECScheme.rep5: 3>
rs8: FECScheme  # value = <FECScheme.rs8: 27>
secded2216: FECScheme  # value = <FECScheme.secded2216: 8>
secded3932: FECScheme  # value = <FECScheme.secded3932: 9>
secded7264: FECScheme  # value = <FECScheme.secded7264: 10>
sqam128: ModulationScheme  # value = <ModulationScheme.sqam128: 43>
sqam32: ModulationScheme  # value = <ModulationScheme.sqam32: 42>
unknown: ModulationScheme  # value = <ModulationScheme.unknown: 0>
v27: FECScheme  # value = <FECScheme.v27: 11>
v27p23: FECScheme  # value = <FECScheme.v27p23: 15>
v27p34: FECScheme  # value = <FECScheme.v27p34: 16>
v27p45: FECScheme  # value = <FECScheme.v27p45: 17>
v27p56: FECScheme  # value = <FECScheme.v27p56: 18>
v27p67: FECScheme  # value = <FECScheme.v27p67: 19>
v27p78: FECScheme  # value = <FECScheme.v27p78: 20>
v29: FECScheme  # value = <FECScheme.v29: 12>
v29p23: FECScheme  # value = <FECScheme.v29p23: 21>
v29p34: FECScheme  # value = <FECScheme.v29p34: 22>
v29p45: FECScheme  # value = <FECScheme.v29p45: 23>
v29p56: FECScheme  # value = <FECScheme.v29p56: 24>
v29p67: FECScheme  # value = <FECScheme.v29p67: 25>
v29p78: FECScheme  # value = <FECScheme.v29p78: 26>
v39: FECScheme  # value = <FECScheme.v39: 13>
v615: FECScheme  # value = <FECScheme.v615: 14>
