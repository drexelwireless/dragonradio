from __future__ import annotations
import numpy
import typing
from . import firpm
__all__ = ['DownsamplerCCC', 'DownsamplerCCF', 'FDDownsamplerCCC', 'FDResamplerCCC', 'FDUpsamplerCCC', 'FIRCCC', 'FIRCCF', 'FilterCC', 'LiquidBaseNCO', 'LiquidFIRCCC', 'LiquidIIRCCC', 'LiquidMSResampCCF', 'LiquidNCO', 'LiquidVCO', 'MixingRationalResamplerCCC', 'NCO', 'PfbCCC', 'PfbCCF', 'RationalResamplerCC', 'RationalResamplerCCC', 'RationalResamplerCCF', 'ResamplerCC', 'TableNCO', 'UpsamplerCCC', 'UpsamplerCCF', 'WindowC', 'butter_lowpass', 'compressIQData', 'convert2fc32', 'convert2sc16', 'decompressIQData', 'exportWisdom', 'firpm', 'importWisdom', 'kaiser', 'parks_mcclellan', 'planFFTs']
class DownsamplerCCC(PfbCCC, ResamplerCC):
    def __init__(self, m: int, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a polyphase downsampler
        """
class DownsamplerCCF(PfbCCF, ResamplerCC):
    def __init__(self, m: int, taps: list[float] = [1.0]) -> None:
        """
        Construct a polyphase downsampler
        """
class FDDownsamplerCCC(ResamplerCC):
    L: typing.ClassVar[int] = 67200
    N: typing.ClassVar[int] = 76800
    O: typing.ClassVar[int] = 9600
    P: typing.ClassVar[int] = 9601
    V: typing.ClassVar[int] = 8
    def __init__(self, X: int, D: int, theta: float, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a frequency domain downsampler
        """
class FDResamplerCCC(RationalResamplerCC):
    L: typing.ClassVar[int] = 67200
    N: typing.ClassVar[int] = 76800
    O: typing.ClassVar[int] = 9600
    P: typing.ClassVar[int] = 9601
    V: typing.ClassVar[int] = 8
    def __init__(self, I: int, D: int, X: int = 1, theta: float = 0.0, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a frequency domain resampler
        """
    @property
    def exact(self) -> bool:
        """
        bool: If True, then resampling is exact
        """
    @exact.setter
    def exact(self, arg1: bool) -> None:
        ...
    @property
    def parallel(self) -> bool:
        """
        bool: If True, then resampling is parallelizable
        """
    @parallel.setter
    def parallel(self, arg1: bool) -> None:
        ...
class FDUpsamplerCCC(ResamplerCC):
    L: typing.ClassVar[int] = 67200
    N: typing.ClassVar[int] = 76800
    P: typing.ClassVar[int] = 9601
    V: typing.ClassVar[int] = 8
    def __init__(self, X: int, I: int, theta: float) -> None:
        """
        Construct a frequency domain upsampler
        """
class FIRCCC(FilterCC):
    def __init__(self, arg0: list[complex]) -> None:
        ...
    @property
    def delay(self) -> float:
        """
        Return filter delay
        """
    @property
    def taps(self) -> list[complex]:
        """
        Filter taps
        """
    @taps.setter
    def taps(self, arg1: list[complex]) -> None:
        ...
class FIRCCF(FilterCC):
    def __init__(self, arg0: list[float]) -> None:
        ...
    @property
    def delay(self) -> float:
        """
        Return filter delay
        """
    @property
    def taps(self) -> list[float]:
        """
        Filter taps
        """
    @taps.setter
    def taps(self, arg1: list[float]) -> None:
        ...
class FilterCC:
    def execute(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Execute the filter
        """
    def groupDelay(self, arg0: float) -> float:
        """
        Return filter group delay of given frequency
        """
    def reset(self) -> None:
        """
        Reset the filter's state
        """
class LiquidBaseNCO(NCO):
    pass
class LiquidFIRCCC(FilterCC):
    def __init__(self, arg0: list[complex]) -> None:
        ...
    @property
    def delay(self) -> float:
        """
        Return filter delay
        """
    @property
    def taps(self) -> list[complex]:
        """
        Filter taps
        """
    @taps.setter
    def taps(self, arg1: list[complex]) -> None:
        ...
class LiquidIIRCCC(FilterCC):
    @typing.overload
    def __init__(self, arg0: numpy.ndarray, arg1: numpy.ndarray) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: numpy.ndarray) -> None:
        ...
class LiquidMSResampCCF(ResamplerCC):
    def __init__(self, rate: float, m: int, fs: float, As: float, npfb: int) -> None:
        """
        Construct a liquid-dsp mult-stage resampler
        """
class LiquidNCO(LiquidBaseNCO):
    def __init__(self, arg0: float) -> None:
        ...
class LiquidVCO(LiquidBaseNCO):
    def __init__(self, arg0: float) -> None:
        ...
class MixingRationalResamplerCCC(RationalResamplerCCC):
    def __init__(self, l: int, m: int, theta: float, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a polyphase mixing rational resampler
        """
    def resampleMixDown(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Resample signal and mix down
        """
    def resampleMixUp(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Mix up and resample signal
        """
    @property
    def bandpass_taps(self) -> list[complex]:
        """
        Prototype bandpass filter taps
        """
    @property
    def theta(self) -> float:
        """
        float: Mixing frequency shift (normalized frequency)
        """
    @theta.setter
    def theta(self, arg1: float) -> None:
        ...
class NCO:
    def mix_down(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Mix signal down
        """
    def mix_up(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Mix signal up
        """
    def reset(self, arg0: float) -> None:
        """
        Reset NCO state given frequency in radians per sample
        """
    @property
    def frequency(self) -> float:
        """
        Frequency in radians per sample
        """
    @frequency.setter
    def frequency(self, arg1: float) -> None:
        ...
    @property
    def phase(self) -> float:
        """
        Phase in radians
        """
    @phase.setter
    def phase(self, arg1: float) -> None:
        ...
class PfbCCC:
    @property
    def channel_taps(self) -> list[list[complex]]:
        """
        Per-channel taps (reversed)
        """
    @property
    def nchannels(self) -> int:
        """
        Number of channels
        """
    @nchannels.setter
    def nchannels(self, arg1: int) -> None:
        ...
    @property
    def taps(self) -> list[complex]:
        """
        Prototype filter taps
        """
    @taps.setter
    def taps(self, arg1: list[complex]) -> None:
        ...
class PfbCCF:
    @property
    def channel_taps(self) -> list[list[float]]:
        """
        Per-channel taps (reversed)
        """
    @property
    def nchannels(self) -> int:
        """
        Number of channels
        """
    @nchannels.setter
    def nchannels(self, arg1: int) -> None:
        ...
    @property
    def taps(self) -> list[float]:
        """
        Prototype filter taps
        """
    @taps.setter
    def taps(self, arg1: list[float]) -> None:
        ...
class RationalResamplerCC(ResamplerCC):
    @property
    def decimation_rate(self) -> int:
        """
        int: Decimation rate
        """
    @property
    def interpolation_rate(self) -> int:
        """
        int: Interpolation rate
        """
class RationalResamplerCCC(PfbCCC, RationalResamplerCC):
    def __init__(self, l: int, m: int, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a polyphase rational resampler
        """
class RationalResamplerCCF(PfbCCF, RationalResamplerCC):
    def __init__(self, l: int, m: int, taps: list[float] = [1.0]) -> None:
        """
        Construct a polyphase rational resampler
        """
class ResamplerCC:
    def resample(self, arg0: numpy.ndarray) -> numpy.ndarray:
        """
        Resample signal
        """
    def reset(self) -> None:
        """
        Reset resampler state
        """
    @property
    def delay(self) -> float:
        """
        Resampling delay
        """
    @property
    def rate(self) -> float:
        """
        Resampling rate
        """
class TableNCO(NCO):
    def __init__(self, arg0: float) -> None:
        ...
class UpsamplerCCC(PfbCCC, ResamplerCC):
    def __init__(self, l: int, taps: list[complex] = [(1+0j)]) -> None:
        """
        Construct a polyphase upsampler
        """
class UpsamplerCCF(PfbCCF, ResamplerCC):
    def __init__(self, l: int, taps: list[float] = [1.0]) -> None:
        """
        Construct a polyphase upsampler
        """
class WindowC:
    def __init__(self, arg0: int) -> None:
        ...
    def add(self, arg0: complex) -> None:
        """
        Add element to window
        """
    def reset(self) -> None:
        """
        Reset window
        """
    def resize(self, arg0: int) -> None:
        """
        Resize window
        """
    @property
    def size(self) -> int:
        """
        Window size
        """
    @property
    def window(self) -> list[complex]:
        """
        Get values in the window
        """
def butter_lowpass(arg0: int, arg1: float, arg2: float, arg3: float, arg4: float) -> tuple[list[float], list[float]]:
    ...
def compressIQData(arg0: numpy.ndarray) -> numpy.ndarray:
    """
    compress fc32 samples
    """
def convert2fc32(arg0: numpy.ndarray) -> numpy.ndarray:
    """
    convert sc16 buffer to a fc32 buffer
    """
def convert2sc16(arg0: numpy.ndarray) -> numpy.ndarray:
    """
    convert fc32 buffer to a sc16 buffer
    """
def decompressIQData(arg0: numpy.ndarray) -> numpy.ndarray:
    """
    decompress fc32 samples
    """
def exportWisdom(arg0: str) -> None:
    """
    export FFTW wisdom
    """
def importWisdom(arg0: str) -> None:
    """
    import FFTW wisdom
    """
def kaiser(arg0: int, arg1: float, arg2: float) -> list[float]:
    ...
def parks_mcclellan(arg0: int, arg1: float, arg2: float) -> list[float]:
    ...
def planFFTs(arg0: int) -> None:
    """
    plan FFTs
    """
