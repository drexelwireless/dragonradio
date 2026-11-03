"""Static downlink OFDMA reference PHY, independent of UHD and liquidDSP.

One IFFT per symbol combines both users. Receivers acquire the burst and estimate
their channel from transmitted training, without transmitter timing/channel hints.
The static allocation and maximum payload size are configured out of band.
"""

from dataclasses import dataclass, field
import math
import struct
from typing import Optional, Tuple
import zlib

import numpy as np


_HEADER = struct.Struct("!4sBBIH")  # magic/version, source, destination, sequence, length
_MAGIC = b"DOF1"
_CRC = struct.Struct("!I")
MODULATIONS = {"qpsk": 2, "16qam": 4}
_A16 = 1 / np.sqrt(10)


@dataclass(frozen=True)
class Packet:
    source: int
    destination: int
    sequence: int
    payload: bytes

    def __post_init__(self):
        if not 0 <= self.source <= 254 or not 1 <= self.destination <= 254:
            raise ValueError("source/destination must fit unicast node IDs")
        if not 0 <= self.sequence <= 0xffffffff:
            raise ValueError("sequence must fit uint32")
        if not isinstance(self.payload, bytes) or len(self.payload) > 65535:
            raise ValueError("payload must be bytes with length <= 65535")

    def encode(self):
        body = _HEADER.pack(_MAGIC, self.source, self.destination, self.sequence, len(self.payload)) + self.payload
        return body + _CRC.pack(zlib.crc32(body))


@dataclass(frozen=True)
class Allocation:
    user_id: int
    carriers: Tuple[int, ...]
    pilots: Tuple[int, ...]

    @property
    def data(self):
        return tuple(k for k in self.carriers if k not in self.pilots)


def _default_allocations():
    return (Allocation(1, tuple(range(-52, -4)), (-52, -40, -28, -16)),
            Allocation(2, tuple(range(5, 53)), (5, 17, 29, 41)))


@dataclass(frozen=True)
class OFDMAConfig:
    nfft: int = 128
    cp_len: int = 16
    sample_rate: float = 1e6
    max_payload: int = 256
    allocations: Tuple[Allocation, ...] = field(default_factory=_default_allocations)
    # Static data-tone constellation, configured out of band at both ends.
    # "qpsk" is the v1 wire format; "16qam" is Gray-mapped square 16-QAM.
    modulation: str = "qpsk"

    def __post_init__(self):
        if not isinstance(self.nfft, int) or self.nfft < 16 or self.nfft % 2:
            raise ValueError("nfft must be an even integer >= 16")
        if not isinstance(self.cp_len, int) or not 1 <= self.cp_len < self.nfft:
            raise ValueError("cp_len must be in [1, nfft)")
        if not np.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError("sample_rate must be positive and finite")
        if not isinstance(self.max_payload, int) or not 0 <= self.max_payload <= 65535:
            raise ValueError("invalid maximum payload")
        if self.modulation not in MODULATIONS:
            raise ValueError("modulation must be one of " + ", ".join(MODULATIONS))
        if len(self.allocations) not in (1, 2) or len({a.user_id for a in self.allocations}) != len(self.allocations):
            raise ValueError("configure one OFDM user or two distinct OFDMA users")
        occupied = set()
        for a in self.allocations:
            bins = set(a.carriers)
            if not 1 <= a.user_id <= 254:
                raise ValueError("invalid allocation user ID")
            if len(bins) != len(a.carriers) or occupied & bins:
                raise ValueError("duplicate or overlapping subcarriers")
            if any(not isinstance(k, int) or k == 0 or not -self.nfft // 2 < k < self.nfft // 2 for k in bins):
                raise ValueError("carriers must exclude DC and Nyquist and fit the FFT")
            if len(set(a.pilots)) != len(a.pilots) or len(a.pilots) < 2 or not set(a.pilots) <= bins or not a.data:
                raise ValueError("each allocation needs data and >= 2 distinct pilots")
            occupied.update(bins)

    @property
    def symbol_samples(self):
        return self.nfft + self.cp_len

    @property
    def bits_per_symbol(self):
        return MODULATIONS[self.modulation]

    @property
    def data_symbols(self):
        bits = (_HEADER.size + self.max_payload + _CRC.size) * 8
        return max(math.ceil(bits / (self.bits_per_symbol * len(a.data))) for a in self.allocations)

    @property
    def frame_samples(self):
        return (2 + self.data_symbols) * self.symbol_samples

    def allocation(self, user_id):
        for a in self.allocations:
            if a.user_id == user_id:
                return a
        raise ValueError("user has no allocation")


def _indices(carriers, nfft):
    return np.asarray(carriers, dtype=int) % nfft


def _qpsk(bits):
    b = np.asarray(bits).reshape(-1, 2).astype(float)
    return ((1 - 2 * b[:, 0]) + 1j * (1 - 2 * b[:, 1])) / np.sqrt(2)


def _map(bits, bits_per_symbol=2):
    """Unit-average-power mapping; 16-QAM is Gray per axis (sign, magnitude)."""
    if bits_per_symbol == 2:
        return _qpsk(bits)
    b = np.asarray(bits).reshape(-1, 4).astype(float)
    return ((1 - 2 * b[:, 0]) * (1 + 2 * b[:, 1]) + 1j * (1 - 2 * b[:, 2]) * (1 + 2 * b[:, 3])) * _A16


def _hard_bits(symbols, bits_per_symbol=2):
    if bits_per_symbol == 2:
        out = np.empty(2 * len(symbols), dtype=np.uint8)
        out[0::2] = np.real(symbols) < 0
        out[1::2] = np.imag(symbols) < 0
        return out
    out = np.empty(4 * len(symbols), dtype=np.uint8)
    out[0::4] = np.real(symbols) < 0
    out[1::4] = np.abs(np.real(symbols)) > 2 * _A16
    out[2::4] = np.imag(symbols) < 0
    out[3::4] = np.abs(np.imag(symbols)) > 2 * _A16
    return out


def _scramble(bits, user_id):
    # Fixed per-user PRBS known at both ends, including padding. This avoids
    # large coherent peaks from long all-zero payloads or padded allocations.
    sequence = np.random.default_rng(6701 + user_id).integers(0, 2, len(bits), dtype=np.uint8)
    return np.bitwise_xor(bits, sequence)


def _training(config):
    grid = np.zeros(config.nfft, dtype=np.complex128)
    bins = sorted(k for a in config.allocations for k in a.carriers)
    # Fixed known sequence, regenerated locally by each receiver.
    grid[_indices(bins, config.nfft)] = 1 - 2 * np.random.default_rng(1921).integers(0, 2, len(bins))
    return grid


def _pilot(symbol_index, pilot_index, user_id):
    return 1.0 if (symbol_index + pilot_index + user_id) % 2 == 0 else -1.0


def _waveform(grid, config):
    td = np.fft.ifft(grid, axis=-1, norm="ortho")
    return np.concatenate((td[:, -config.cp_len:], td), axis=1).reshape(-1)


class OFDMATransmitter:
    def __init__(self, config: OFDMAConfig = OFDMAConfig()):
        self.config = config
        self.training = _training(config)
        self.maps = {}
        for a in config.allocations:
            capacity = config.bits_per_symbol * config.data_symbols * len(a.data)
            prbs = _scramble(np.zeros(capacity, dtype=np.uint8), a.user_id)
            pilots = 1 - 2 * ((np.arange(config.data_symbols)[:, None] + np.arange(len(a.pilots))[None, :] + a.user_id) % 2)
            self.maps[a.user_id] = (_indices(a.data, config.nfft), _indices(a.pilots, config.nfft), prbs, pilots)

    def build_grid(self, packets):
        cfg = self.config
        packets = tuple(packets)
        by_user = {p.destination: p for p in packets}
        if len(by_user) != len(packets) or set(by_user) != {a.user_id for a in cfg.allocations}:
            raise ValueError("provide exactly one packet for each allocated user")
        if len({p.source for p in packets}) != 1:
            raise ValueError("a frame must have one source")
        grid = np.zeros((2 + cfg.data_symbols, cfg.nfft), dtype=np.complex128)
        grid[:2] = self.training
        for a in cfg.allocations:
            packet = by_user[a.user_id]
            if len(packet.payload) > cfg.max_payload:
                raise ValueError("payload exceeds configured frame capacity")
            bits = np.unpackbits(np.frombuffer(packet.encode(), dtype=np.uint8))
            capacity = cfg.bits_per_symbol * cfg.data_symbols * len(a.data)
            data_bins, pilot_bins, prbs, pilots = self.maps[a.user_id]
            symbols = _map(np.bitwise_xor(np.pad(bits, (0, capacity - bits.size)), prbs), cfg.bits_per_symbol)
            grid[2:, data_bins] = symbols.reshape(cfg.data_symbols, -1)
            grid[2:, pilot_bins] = pilots
        return grid

    def modulate(self, packets):
        return _waveform(self.build_grid(packets), self.config).astype(np.complex64)


@dataclass
class DecodeResult:
    user_id: int
    packet: Optional[Packet] = None
    error: Optional[str] = None
    start_sample: Optional[int] = None
    cfo_subcarriers: Optional[float] = None
    sync_metric: Optional[float] = None
    evm_db: Optional[float] = None
    correction_trials: int = 0

    @property
    def ok(self):
        return self.packet is not None and self.error is None


class OFDMAReceiver:
    def __init__(self, user_id: int, config: OFDMAConfig = OFDMAConfig()):
        self.config = config
        self.allocation = config.allocation(user_id)
        a = self.allocation
        self.training = _training(config)
        self.template = _waveform(np.stack([self.training, self.training]), config)
        self.template_energy = np.vdot(self.template, self.template).real
        self.bins = _indices(a.carriers, config.nfft)
        self.data_pos = [a.carriers.index(k) for k in a.data]
        self.pilot_pos = [a.carriers.index(k) for k in a.pilots]
        self.pilots = 1 - 2 * ((np.arange(config.data_symbols)[:, None] + np.arange(len(a.pilots))[None, :] + a.user_id) % 2)
        self.prbs = _scramble(np.zeros(config.bits_per_symbol * config.data_symbols * len(a.data), dtype=np.uint8), user_id)
        self.time_index = np.arange(config.frame_samples)

    def demodulate(self, samples):
        cfg, a = self.config, self.allocation
        result = DecodeResult(a.user_id)
        y = np.asarray(samples, dtype=np.complex128)
        if y.ndim != 1 or y.size < cfg.frame_samples or not np.all(np.isfinite(y)):
            result.error = "short or invalid IQ burst"
            return result
        training, template = self.training, self.template
        # Known-preamble correlation. Search only starts with a complete frame.
        count = y.size - cfg.frame_samples + 1
        corr = np.correlate(y[:count + len(template) - 1], template, mode="valid")
        energy = np.concatenate(([0.0], np.cumsum(np.abs(y[:count + len(template) - 1]) ** 2)))
        energy = energy[len(template):] - energy[:-len(template)]
        score = np.abs(corr) ** 2 / np.maximum(energy[:count] * self.template_energy, 1e-20)
        start = int(np.argmax(score))
        result.start_sample, result.sync_metric = start, float(score[start])
        if score[start] < 0.20:
            result.error = "preamble acquisition failed"
            return result
        length = cfg.symbol_samples
        first = y[start + cfg.cp_len:start + length]
        second = y[start + length + cfg.cp_len:start + 2 * length]
        omega = np.angle(np.vdot(first, second)) / length
        result.cfo_subcarriers = float(omega * cfg.nfft / (2 * np.pi))
        frame = y[start:start + cfg.frame_samples] * np.exp(-1j * omega * self.time_index)
        symbols = frame.reshape(-1, length)[:, cfg.cp_len:]
        grid = np.fft.fft(symbols, axis=1, norm="ortho")
        bins = self.bins
        estimate = grid[:2, bins].mean(axis=0) / training[bins]
        if np.min(np.abs(estimate)) < 1e-8:
            result.error = "channel estimate has a spectral null"
            return result
        equalized = grid[2:, bins] / estimate
        phase = np.angle(np.sum(self.pilots * equalized[:, self.pilot_pos], axis=1))
        recovered = (equalized[:, self.data_pos] * np.exp(-1j * phase[:, None])).reshape(-1)
        bits = _hard_bits(recovered, cfg.bits_per_symbol)
        decisions = _map(bits, cfg.bits_per_symbol)
        # Decision-directed EVM, not a comparison with transmitter ground truth.
        result.evm_db = float(10 * np.log10(max(float(np.mean(np.abs(recovered - decisions) ** 2)), 1e-20)))
        raw = np.packbits(np.bitwise_xor(bits, self.prbs)).tobytes()
        magic, source, destination, sequence, size = _HEADER.unpack_from(raw)
        if magic != _MAGIC or destination != a.user_id or source > 254 or size > cfg.max_payload:
            result.error = "invalid packet header"
            return result
        end = _HEADER.size + size
        if _CRC.unpack_from(raw, end)[0] != zlib.crc32(raw[:end]):
            result.error = "payload CRC failed"
            return result
        result.packet = Packet(source, destination, sequence, raw[_HEADER.size:end])
        return result


def ofdm_config(user_id=1, **kwargs):
    """Single-user OFDM on the same occupied bandwidth and pilot locations."""
    allocations = _default_allocations()
    carriers = tuple(k for a in allocations for k in a.carriers)
    pilots = tuple(k for a in allocations for k in a.pilots)
    return OFDMAConfig(allocations=(Allocation(user_id, carriers, pilots),), **kwargs)


class OFDMTransmitter(OFDMATransmitter):
    def __init__(self, config=None):
        super().__init__(ofdm_config() if config is None else config)
        if len(self.config.allocations) != 1:
            raise ValueError("OFDMTransmitter requires one user")


class OFDMReceiver(OFDMAReceiver):
    def __init__(self, user_id=1, config=None):
        super().__init__(user_id, ofdm_config(user_id) if config is None else config)
        if len(self.config.allocations) != 1:
            raise ValueError("OFDMReceiver requires one user")
