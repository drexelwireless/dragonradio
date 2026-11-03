"""Framed native OFDM (wire format v2): DragonRadio header + variable payload.

The production adapter (src/phy/NativeOFDM.cc) uses the same C++ engine; this
wrapper exists so the format can be tested and simulated without UHD.
"""
from dataclasses import dataclass
import ctypes

import numpy as np

from .fast import _load, library_path
from .ofdma import _default_allocations

INFO_BYTES = 5


def default_allocation(nfft=128):
    """Full-band OFDM data and pilot FFT bins (same tones as ofdm_config())."""
    allocations = _default_allocations()
    carriers = [k for a in allocations for k in a.carriers]
    pilots = [k for a in allocations for k in a.pilots]
    data = [k % nfft for k in carriers if k not in pilots]
    return data, [k % nfft for k in pilots]


@dataclass
class FrameResult:
    start: int
    samples: int
    header_valid: bool
    wanted: bool
    payload_valid: bool
    info: bytes
    mcsidx: int
    bits_per_symbol: int
    length: int
    payload: bytes
    sync: float
    cfo_subcarriers: float
    evm_db: float
    rssi_db: float
    chase_trials: float

    @property
    def ok(self):
        return self.header_valid and self.wanted and self.payload_valid


class FramedModem:
    """Single-allocation framed modem. bins are FFT indices in [0, nfft)."""
    def __init__(self, nfft=128, cp_len=16, data=None, pilots=None, max_payload=2048, chase_bits=4,
                 user_id=1, library=None):
        if data is None or pilots is None:
            data, pilots = default_allocation(nfft)
        self.nfft, self.cp_len, self.max_payload = nfft, cp_len, max_payload
        self.lib = _load(library or library_path())
        if not hasattr(self.lib, "dr_frame_create"):
            raise RuntimeError("native library lacks the framed engine; rebuild with scripts/build-phy.py")
        ptr, i = ctypes.c_void_p, ctypes.c_int
        sig = {"create": ([i, i, i, ptr, i, ptr, i, i, i], ptr), "destroy": ([ptr], None),
               "samples": ([ptr, i, i], i), "header_symbols": ([ptr], i),
               "write": ([ptr, ptr, i, i, ptr, i, ptr, i], i),
               "stream_create": ([ptr, i], ptr), "stream_destroy": ([ptr], None), "stream_reset": ([ptr], None),
               "stream_pending": ([ptr], ctypes.c_longlong), "stream_open": ([ptr], i),
               "stream_feed": ([ptr, ptr, i, i], i), "stream_next": ([ptr, ptr, ptr, i, ptr], i)}
        for name, (args, res) in sig.items():
            f = getattr(self.lib, "dr_frame_" + name)
            f.argtypes, f.restype = args, res
        d = np.ascontiguousarray(data, dtype=np.int32)
        p = np.ascontiguousarray(pilots, dtype=np.int32)
        self.handle = self.lib.dr_frame_create(nfft, cp_len, d.size, d.ctypes.data, p.size, p.ctypes.data,
                                              max_payload, chase_bits, user_id)
        if not self.handle:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        self.header_symbols = self.lib.dr_frame_header_symbols(self.handle)

    def frame_samples(self, length, bits_per_symbol=2):
        n = self.lib.dr_frame_samples(self.handle, length, bits_per_symbol)
        if n < 0:
            raise ValueError(self.lib.dr_fast_error().decode())
        return n

    def modulate(self, info, payload, mcsidx=0, bits_per_symbol=2):
        info = bytes(info)
        if len(info) != INFO_BYTES:
            raise ValueError("header info must be 5 bytes")
        body = np.frombuffer(bytes(payload), dtype=np.uint8) if payload else np.zeros(1, np.uint8)
        out = np.empty(self.frame_samples(len(payload), bits_per_symbol), dtype=np.complex64)
        hdr = np.frombuffer(info, dtype=np.uint8)
        n = self.lib.dr_frame_write(self.handle, hdr.ctypes.data, mcsidx, bits_per_symbol, body.ctypes.data,
                                    len(payload), out.ctypes.data, out.size)
        if n < 0:
            raise ValueError(self.lib.dr_fast_error().decode())
        return out

    def stream(self, nexthop_filter=None):
        return FramedStream(self, -1 if nexthop_filter is None else nexthop_filter)

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_frame_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()


class FramedStream:
    """Streaming receiver; the modem must outlive it."""
    def __init__(self, modem, nexthop_filter=-1):
        self.modem, self.lib = modem, modem.lib
        self.handle = self.lib.dr_frame_stream_create(modem.handle, nexthop_filter)
        if not self.handle:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        self._info = np.zeros(INFO_BYTES, np.uint8)
        self._payload = np.zeros(modem.max_payload + 1, np.uint8)
        self._meta = np.zeros(13, np.float64)

    @property
    def pending_samples(self):
        return int(self.lib.dr_frame_stream_pending(self.handle))

    @property
    def frame_open(self):
        return bool(self.lib.dr_frame_stream_open(self.handle))

    def feed(self, samples, final=False):
        y = np.ascontiguousarray(samples, dtype=np.complex64)
        if y.ndim != 1 or not np.all(np.isfinite(y)):
            raise ValueError("stream must contain finite one-dimensional IQ")
        queued = self.lib.dr_frame_stream_feed(self.handle, y.ctypes.data if y.size else None, y.size, int(final))
        if queued < 0:
            raise RuntimeError(self.lib.dr_fast_error().decode())
        out = []
        for _ in range(queued):
            n = self.lib.dr_frame_stream_next(self.handle, self._info.ctypes.data, self._payload.ctypes.data,
                                              self._payload.size, self._meta.ctypes.data)
            m = self._meta
            out.append(FrameResult(int(m[0]), int(m[1]), bool(m[2]), bool(m[3]), bool(m[4]), self._info.tobytes(),
                                   int(m[5]), int(m[6]), int(m[7]), self._payload[:max(n, 0)].tobytes(),
                                   float(m[8]), float(m[9]), float(m[10]), float(m[11]), float(m[12])))
        return out

    def reset(self):
        self.lib.dr_frame_stream_reset(self.handle)

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_frame_stream_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()
