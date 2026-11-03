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

    def __post_init__(self):
        if not isinstance(self.nfft, int) or self.nfft < 16 or self.nfft % 2:
            raise ValueError("nfft must be an even integer >= 16")
        if not isinstance(self.cp_len, int) or not 1 <= self.cp_len < self.nfft:
            raise ValueError("cp_len must be in [1, nfft)")
        if not np.isfinite(self.sample_rate) or self.sample_rate <= 0:
            raise ValueError("sample_rate must be positive and finite")
        if not isinstance(self.max_payload, int) or not 0 <= self.max_payload <= 65535:
            raise ValueError("invalid maximum payload")
        if len(self.allocations) != 2 or len({a.user_id for a in self.allocations}) != 2:
            raise ValueError("this prototype requires exactly two distinct users")
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
    def data_symbols(self):
        bits = (_HEADER.size + self.max_payload + _CRC.size) * 8
        return max(math.ceil(bits / (2 * len(a.data))) for a in self.allocations)

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


def _hard_bits(symbols):
    out = np.empty(2 * len(symbols), dtype=np.uint8)
    out[0::2] = np.real(symbols) < 0
    out[1::2] = np.imag(symbols) < 0
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

    def build_grid(self, packets):
        cfg = self.config
        packets = tuple(packets)
        by_user = {p.destination: p for p in packets}
        if len(by_user) != len(packets) or set(by_user) != {a.user_id for a in cfg.allocations}:
            raise ValueError("provide exactly one packet for each allocated user")
        if len({p.source for p in packets}) != 1 or len({p.sequence for p in packets}) != 1:
            raise ValueError("a frame must have one source and one sequence number")
        grid = np.zeros((2 + cfg.data_symbols, cfg.nfft), dtype=np.complex128)
        grid[:2] = _training(cfg)
        for a in cfg.allocations:
            packet = by_user[a.user_id]
            if len(packet.payload) > cfg.max_payload:
                raise ValueError("payload exceeds configured frame capacity")
            bits = np.unpackbits(np.frombuffer(packet.encode(), dtype=np.uint8))
            capacity = 2 * cfg.data_symbols * len(a.data)
            symbols = _qpsk(_scramble(np.pad(bits, (0, capacity - bits.size)), a.user_id))
            grid[2:, _indices(a.data, cfg.nfft)] = symbols.reshape(cfg.data_symbols, -1)
            for s in range(cfg.data_symbols):
                grid[s + 2, _indices(a.pilots, cfg.nfft)] = [_pilot(s, i, a.user_id) for i in range(len(a.pilots))]
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

    @property
    def ok(self):
        return self.packet is not None and self.error is None


class OFDMAReceiver:
    def __init__(self, user_id: int, config: OFDMAConfig = OFDMAConfig()):
        self.config = config
        self.allocation = config.allocation(user_id)

    def demodulate(self, samples):
        cfg, a = self.config, self.allocation
        result = DecodeResult(a.user_id)
        y = np.asarray(samples, dtype=np.complex128)
        if y.ndim != 1 or y.size < cfg.frame_samples or not np.all(np.isfinite(y)):
            result.error = "short or invalid IQ burst"
            return result
        training = _training(cfg)
        template = _waveform(np.stack([training, training]), cfg)
        # Known-preamble correlation. Search only starts with a complete frame.
        count = y.size - cfg.frame_samples + 1
        corr = np.correlate(y, template, mode="valid")[:count]
        energy = np.concatenate(([0.0], np.cumsum(np.abs(y) ** 2)))
        energy = energy[len(template):] - energy[:-len(template)]
        score = np.abs(corr) ** 2 / np.maximum(energy[:count] * np.vdot(template, template).real, 1e-20)
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
        frame = y[start:start + cfg.frame_samples] * np.exp(-1j * omega * np.arange(cfg.frame_samples))
        symbols = frame.reshape(-1, length)[:, cfg.cp_len:]
        grid = np.fft.fft(symbols, axis=1, norm="ortho")
        bins = _indices(a.carriers, cfg.nfft)
        estimate = grid[:2, bins].mean(axis=0) / training[bins]
        if np.min(np.abs(estimate)) < 1e-8:
            result.error = "channel estimate has a spectral null"
            return result
        equalized = grid[2:, bins] / estimate
        data_pos = [a.carriers.index(k) for k in a.data]
        pilot_pos = [a.carriers.index(k) for k in a.pilots]
        recovered = []
        for s, row in enumerate(equalized):
            expected = np.array([_pilot(s, i, a.user_id) for i in range(len(a.pilots))])
            phase = np.angle(np.vdot(expected, row[pilot_pos]))
            recovered.extend(row[data_pos] * np.exp(-1j * phase))
        recovered = np.asarray(recovered)
        bits = _hard_bits(recovered)
        decisions = _qpsk(bits)
        # Decision-directed EVM, not a comparison with transmitter ground truth.
        result.evm_db = float(10 * np.log10(max(float(np.mean(np.abs(recovered - decisions) ** 2)), 1e-20)))
        raw = np.packbits(_scramble(bits, a.user_id)).tobytes()
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
