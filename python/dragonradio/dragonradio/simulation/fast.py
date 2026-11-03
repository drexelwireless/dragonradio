"""Compiled FFTW frame engine, wire-compatible with the NumPy reference PHY.

``library_path()`` is the current (v2) engine. ``library_path("reference-v1")``
is the frozen v1 engine kept only as a benchmark comparator; it supports QPSK
with four-bit Chase search and has no persistent stream.
"""
import ctypes
from pathlib import Path
import platform

import numpy as np

from .ofdma import (DecodeResult, OFDMAConfig, Packet, _CRC, _HEADER, _MAGIC,
                    _scramble, _training)
import zlib

VARIANTS = {"current": "phy", "reference-v1": "phy-v1"}
_ERRORS = {-1: "short or invalid IQ burst", -2: "preamble acquisition failed",
           -3: "channel estimate has a spectral null",
           -6: "invalid packet header"}   # v2 early rejection after the first data symbol


def library_path(variant="current"):
    if variant not in VARIANTS:
        raise ValueError("unknown native engine variant: " + str(variant))
    name = "libdragonradio_phy.dylib" if platform.system() == "Darwin" else "libdragonradio_phy.so"
    return Path(__file__).resolve().parents[4] / "build" / VARIANTS[variant] / name


_LIBRARIES = {}


def _load(path):
    path = str(path)
    if path in _LIBRARIES:
        return _LIBRARIES[path]
    lib = ctypes.CDLL(path)
    ptr, integer, double_p = ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p
    signatures = {"create": ([integer] * 4 + [ptr] * 7, ptr), "destroy": ([ptr], None),
                  "channel_filter": ([ptr, integer, integer, ptr, ptr, ptr], integer),
                  "write": ([ptr, ptr, ptr, ptr, integer], integer),
                  "receive": ([ptr, ptr, integer, integer, ptr, integer, ptr], integer),
                  "error": ([], ctypes.c_char_p)}
    optional = {"version": ([], integer),
                "create2": ([integer] * 4 + [ptr] * 7 + [integer, integer], ptr),
                "stream_create": ([ptr, integer], ptr), "stream_destroy": ([ptr], None),
                "stream_reset": ([ptr], None), "stream_pending": ([ptr], ctypes.c_longlong),
                "stream_feed": ([ptr, ptr, integer, integer], integer),
                "stream_next": ([ptr, ptr, integer, double_p], integer)}
    for table, required in ((signatures, True), (optional, False)):
        for name, (args, result) in table.items():
            f = getattr(lib, "dr_fast_" + name, None)
            if f is None:
                if required:
                    raise RuntimeError("native library lacks dr_fast_" + name)
                continue
            f.argtypes, f.restype = args, result
    lib.dr_version = lib.dr_fast_version() if hasattr(lib, "dr_fast_version") else 1
    _LIBRARIES[path] = lib
    return lib


class FastContext:
    def __init__(self, config, library=None, chase_bits=4):
        self.config = config
        path = Path(library) if library else library_path()
        if not path.exists():
            raise RuntimeError("non-liquidDSP library absent; run python scripts/build-phy.py")
        self.lib = _load(path)
        self.library = str(path)
        self.version = self.lib.dr_version
        bps = config.bits_per_symbol
        if self.version < 2 and (bps != 2 or chase_bits != 4):
            raise ValueError("the v1 engine supports only QPSK with 4-bit Chase search")
        allocations = config.allocations
        arrays = [np.array([a.user_id for a in allocations], dtype=np.int32),
                  np.array([len(a.data) for a in allocations], dtype=np.int32),
                  np.array([k % config.nfft for a in allocations for k in a.data], dtype=np.int32),
                  np.array([len(a.pilots) for a in allocations], dtype=np.int32),
                  np.array([k % config.nfft for a in allocations for k in a.pilots], dtype=np.int32),
                  _training(config).astype(np.complex64),
                  np.concatenate([_scramble(np.zeros(bps * config.data_symbols * len(a.data), dtype=np.uint8), a.user_id)
                                  for a in allocations])]
        self._arrays = arrays
        dims = (config.nfft, config.cp_len, config.data_symbols, len(allocations))
        pointers = [a.ctypes.data for a in arrays]
        if self.version >= 2:
            self.handle = self.lib.dr_fast_create2(*dims, *pointers, bps, chase_bits)
        else:
            self.handle = self.lib.dr_fast_create(*dims, *pointers)
        if not self.handle:
            raise RuntimeError(self.lib.dr_fast_error().decode())

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_fast_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()


class FastTransmitter(FastContext):
    def __init__(self, config=OFDMAConfig(), library=None):
        super().__init__(config, library)
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


def _finish(result, count, raw, metrics, config, user_id):
    """Shared post-processing of one native decode attempt."""
    if count in _ERRORS and count != -1:
        result.start_sample, result.sync_metric = int(metrics[0]), float(metrics[1])
    if count in _ERRORS:
        result.error = _ERRORS[count]
        if count in (-3, -6):
            result.cfo_subcarriers = float(metrics[2])
        return result
    result.start_sample, result.sync_metric = int(metrics[0]), float(metrics[1])
    result.cfo_subcarriers = float(metrics[2])
    result.evm_db = float(metrics[3])
    result.correction_trials = int(metrics[4])
    magic, source, destination, sequence, size = _HEADER.unpack_from(raw)
    if magic != _MAGIC or source > 254 or destination != user_id or size > config.max_payload:
        result.error = "invalid packet header"
        return result
    end = _HEADER.size + size
    if _CRC.unpack_from(raw, end)[0] != zlib.crc32(raw[:end]):
        result.error = "payload CRC failed"
        return result
    result.packet = Packet(source, destination, sequence, raw[_HEADER.size:end])
    return result


class FastReceiver(FastContext):
    def __init__(self, user_id, config=OFDMAConfig(), library=None, chase_bits=4):
        config.allocation(user_id)
        super().__init__(config, library, chase_bits)
        self.user_id = user_id
        self.chase_bits = chase_bits
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
        bits = config.bits_per_symbol * config.data_symbols * len(config.allocation(user_id).data)
        self.buffer = np.empty((bits + 7) // 8, dtype=np.uint8)
        self.metrics = np.zeros(6, dtype=np.float64)

    @property
    def supports_stream(self):
        return self.version >= 2

    def demodulate(self, samples):
        y = np.ascontiguousarray(samples, dtype=np.complex64)
        result = DecodeResult(self.user_id)
        if y.ndim != 1:
            result.error = _ERRORS[-1]
            return result
        count = self.lib.dr_fast_receive(self.handle, y.ctypes.data, y.size, self.user_id,
                                        self.buffer.ctypes.data, self.buffer.size, self.metrics.ctypes.data)
        if count == -4:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        raw = self.buffer[:max(count, 0)].tobytes()
        return _finish(result, count, raw, self.metrics, self.config, self.user_id)


class NativeStream:
    """Persistent C++ streaming receiver (v2 engine only).

    Same contract as frame.StreamReceiver: results for every acquired candidate
    (verified or not), absolute ``start_sample`` offsets, and bounded buffering.
    The underlying receiver must outlive the stream.
    """
    def __init__(self, receiver):
        if not receiver.supports_stream:
            raise ValueError("the persistent stream requires the v2 native engine")
        self.receiver = receiver
        self.config = receiver.config
        self.lib = receiver.lib
        self.handle = self.lib.dr_fast_stream_create(receiver.handle, receiver.user_id)
        if not self.handle:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        self._raw = np.empty(receiver.buffer.size, dtype=np.uint8)
        self._metrics = np.zeros(6, dtype=np.float64)

    @property
    def pending_samples(self):
        return int(self.lib.dr_fast_stream_pending(self.handle))

    def feed(self, samples, final=False):
        y = np.ascontiguousarray(samples, dtype=np.complex64)
        if y.ndim != 1 or not np.all(np.isfinite(y)):
            raise ValueError("stream must contain finite one-dimensional IQ")
        queued = self.lib.dr_fast_stream_feed(self.handle, y.ctypes.data if y.size else None, y.size, int(final))
        if queued < 0:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        results = []
        for _ in range(queued):
            count = self.lib.dr_fast_stream_next(self.handle, self._raw.ctypes.data, self._raw.size, self._metrics.ctypes.data)
            result = DecodeResult(self.receiver.user_id)
            results.append(_finish(result, count, self._raw[:max(count, 0)].tobytes(), self._metrics,
                                   self.config, self.receiver.user_id))
        return results

    def reset(self):
        self.lib.dr_fast_stream_reset(self.handle)

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_fast_stream_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()


def make_phy(config, implementation="auto", library=None, chase_bits=4):
    if implementation not in ("auto", "native", "numpy"):
        raise ValueError("unknown PHY engine")
    path = Path(library) if library else library_path()
    native = implementation == "native" or (implementation == "auto" and path.exists())
    if native:
        return (FastTransmitter(config, library), {a.user_id: FastReceiver(a.user_id, config, library, chase_bits)
                                                  for a in config.allocations}, "native-fftw")
    from .ofdma import OFDMATransmitter, OFDMAReceiver
    return OFDMATransmitter(config), {a.user_id: OFDMAReceiver(a.user_id, config) for a in config.allocations}, "numpy"
