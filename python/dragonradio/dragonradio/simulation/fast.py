"""Compiled FFTW frame engine, wire-compatible with the NumPy reference PHY."""
import ctypes
from pathlib import Path
import platform

import numpy as np

from .ofdma import (DecodeResult, OFDMAConfig, Packet, _CRC, _HEADER, _MAGIC,
                    _scramble, _training)
import zlib


def library_path():
    return Path(__file__).resolve().parents[4] / "build" / "phy" / ("libdragonradio_phy.dylib" if platform.system() == "Darwin" else "libdragonradio_phy.so")


class FastContext:
    def __init__(self, config):
        self.config = config
        path = library_path()
        if not path.exists():
            raise RuntimeError("non-liquidDSP library absent; run python scripts/build-phy.py")
        self.lib = ctypes.CDLL(str(path))
        ptr, integer = ctypes.c_void_p, ctypes.c_int
        signatures = {"create": ([integer] * 4 + [ptr] * 7, ptr), "destroy": ([ptr], None),
                      "channel_filter": ([ptr, integer, integer, ptr, ptr, ptr], integer),
                      "write": ([ptr, ptr, ptr, ptr, integer], integer),
                      "receive": ([ptr, ptr, integer, integer, ptr, integer, ptr], integer),
                      "error": ([], ctypes.c_char_p)}
        for name, (args, result) in signatures.items():
            f = getattr(self.lib, "dr_fast_" + name)
            f.argtypes, f.restype = args, result
        allocations = config.allocations
        arrays = [np.array([a.user_id for a in allocations], dtype=np.int32),
                  np.array([len(a.data) for a in allocations], dtype=np.int32),
                  np.array([k % config.nfft for a in allocations for k in a.data], dtype=np.int32),
                  np.array([len(a.pilots) for a in allocations], dtype=np.int32),
                  np.array([k % config.nfft for a in allocations for k in a.pilots], dtype=np.int32),
                  _training(config).astype(np.complex64),
                  np.concatenate([_scramble(np.zeros(2 * config.data_symbols * len(a.data), dtype=np.uint8), a.user_id) for a in allocations])]
        self.handle = self.lib.dr_fast_create(config.nfft, config.cp_len, config.data_symbols,
                                              len(allocations), *[a.ctypes.data for a in arrays])
        if not self.handle:
            raise RuntimeError(self.lib.dr_fast_error().decode())

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_fast_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()


class FastTransmitter(FastContext):
    def __init__(self, config=OFDMAConfig()):
        super().__init__(config)
        self.users = {a.user_id for a in config.allocations}

    def modulate(self, packets):
        packets = tuple(packets)
        by_user = {p.destination: p for p in packets}
        if len(by_user) != len(packets) or set(by_user) != self.users or len({p.source for p in packets}) != 1:
            raise ValueError("one source and exactly one packet per allocated user required")
        if any(len(p.payload) > self.config.max_payload for p in packets):
            raise ValueError("payload exceeds configured frame capacity")
        encoded = [by_user[a.user_id].encode() for a in self.config.allocations]
        sizes = np.array([len(p) for p in encoded], dtype=np.int32)
        data = np.frombuffer(b"".join(encoded), dtype=np.uint8)
        out = np.empty(self.config.frame_samples, dtype=np.complex64)
        count = self.lib.dr_fast_write(self.handle, data.ctypes.data, sizes.ctypes.data, out.ctypes.data, out.size)
        if count < 0:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        return out


class FastReceiver(FastContext):
    def __init__(self, user_id, config=OFDMAConfig()):
        config.allocation(user_id)
        super().__init__(config)
        self.user_id = user_id
        # A least-squares CP-limited channel model suppresses training noise.
        # Geometry is static and cached here; no simulator state is consulted.
        active = np.flatnonzero(_training(config)).astype(np.int32)
        maximum = min(config.cp_len + 1, len(active))
        for ntaps in sorted(set(min(n, maximum) for n in (1, 2, 4, 8, maximum))):
            basis = np.exp(-2j * np.pi * active[:, None] * np.arange(ntaps) / config.nfft)
            fit = np.ascontiguousarray(np.linalg.pinv(basis), dtype=np.complex64)
            basis = np.ascontiguousarray(basis, dtype=np.complex64)
            if self.lib.dr_fast_channel_filter(self.handle, ntaps, len(active), active.ctypes.data, fit.ctypes.data, basis.ctypes.data):
                raise RuntimeError(self.lib.dr_fast_error().decode())
        self.buffer = np.empty((2 * config.data_symbols * len(config.allocation(user_id).data) + 7) // 8, dtype=np.uint8)
        self.metrics = np.zeros(5, dtype=np.float64)

    def demodulate(self, samples):
        y = np.ascontiguousarray(samples, dtype=np.complex64)
        result = DecodeResult(self.user_id)
        if y.ndim != 1:
            result.error = "short or invalid IQ burst"
            return result
        count = self.lib.dr_fast_receive(self.handle, y.ctypes.data, y.size, self.user_id,
                                        self.buffer.ctypes.data, self.buffer.size, self.metrics.ctypes.data)
        if count == -4:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        if count == -1:
            result.error = "short or invalid IQ burst"
            return result
        result.start_sample, result.sync_metric = int(self.metrics[0]), float(self.metrics[1])
        if count == -2:
            result.error = "preamble acquisition failed"
            return result
        result.cfo_subcarriers = float(self.metrics[2])
        if count == -3:
            result.error = "channel estimate has a spectral null"
            return result
        result.evm_db = float(self.metrics[3])
        result.correction_trials = int(self.metrics[4])
        raw = self.buffer[:count].tobytes()
        magic, source, destination, sequence, size = _HEADER.unpack_from(raw)
        if magic != _MAGIC or source > 254 or destination != self.user_id or size > self.config.max_payload:
            result.error = "invalid packet header"
            return result
        end = _HEADER.size + size
        if _CRC.unpack_from(raw, end)[0] != zlib.crc32(raw[:end]):
            result.error = "payload CRC failed"
            return result
        result.packet = Packet(source, destination, sequence, raw[_HEADER.size:end])
        return result


def make_phy(config, implementation="auto"):
    if implementation not in ("auto", "native", "numpy"):
        raise ValueError("unknown PHY engine")
    native = implementation == "native" or (implementation == "auto" and library_path().exists())
    if native:
        return FastTransmitter(config), {a.user_id: FastReceiver(a.user_id, config) for a in config.allocations}, "native-fftw"
    from .ofdma import OFDMATransmitter, OFDMAReceiver
    return OFDMATransmitter(config), {a.user_id: OFDMAReceiver(a.user_id, config) for a in config.allocations}, "numpy"
