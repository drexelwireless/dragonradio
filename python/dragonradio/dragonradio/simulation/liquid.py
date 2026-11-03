"""ctypes adapter for the actual DragonRadio liquidDSP OFDM wrappers."""
import ctypes
from pathlib import Path
import platform
import struct
import zlib

import numpy as np


class LiquidOFDM:
    def __init__(self, config, library=None):
        if len(config.allocations) != 1:
            raise ValueError("liquid OFDM comparison needs one full-band allocation")
        root = Path(__file__).resolve().parents[4]
        path = Path(library) if library else root / "build" / "modem" / ("libdragonradio_modem.dylib" if platform.system() == "Darwin" else "libdragonradio_modem.so")
        if not path.exists():
            raise RuntimeError("modem bridge absent; run python scripts/build-modem.py --fetch")
        self.lib = ctypes.CDLL(str(path))
        pointer, size, long = ctypes.c_void_p, ctypes.c_size_t, ctypes.c_long
        signatures = {
            "create": ([ctypes.c_uint, ctypes.c_uint, ctypes.c_char_p], pointer),
            "create_mcs": ([ctypes.c_uint, ctypes.c_uint, ctypes.c_char_p, ctypes.c_int], pointer),
            "destroy": ([pointer], None),
            "assemble": ([pointer, pointer, size], long),
            "write": ([pointer, pointer, size], long),
            "receive": ([pointer, pointer, size, pointer, size, ctypes.c_int], long),
            "receive_many": ([pointer, pointer, size, pointer, size, ctypes.c_int], long),
            "error": ([], ctypes.c_char_p),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, "dr_liquid_" + name, None)
            if function is None:
                if name == "create_mcs":
                    continue   # bridge built before 16-QAM support
                raise RuntimeError("modem bridge lacks dr_liquid_" + name)
            function.argtypes, function.restype = arguments, result
        mask = ["."] * config.nfft
        allocation = config.allocations[0]
        for k in allocation.carriers:
            mask[k % config.nfft] = "P" if k in allocation.pilots else "+"
        bps = getattr(config, "bits_per_symbol", 2)
        if bps == 2:
            self.handle = self.lib.dr_liquid_create(config.nfft, config.cp_len, "".join(mask).encode())
        elif hasattr(self.lib, "dr_liquid_create_mcs") and self.lib.dr_liquid_create_mcs.argtypes:
            self.handle = self.lib.dr_liquid_create_mcs(config.nfft, config.cp_len, "".join(mask).encode(), bps)
        else:
            raise RuntimeError("modem bridge lacks 16-QAM support; rebuild with scripts/build-modem.py")
        if not self.handle:
            raise RuntimeError(self.lib.dr_liquid_error().decode())
        self.buffer = np.empty(config.max_payload + 256, dtype=np.uint8)

    def close(self):
        if getattr(self, "handle", None):
            self.lib.dr_liquid_destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()

    def modulate(self, packet):
        payload = np.frombuffer(packet.encode(), dtype=np.uint8)
        length = self.lib.dr_liquid_assemble(self.handle, payload.ctypes.data, payload.size)
        if length < 0:
            raise RuntimeError(self.lib.dr_liquid_error().decode())
        out = np.empty(length, dtype=np.complex64)
        written = self.lib.dr_liquid_write(self.handle, out.ctypes.data, out.size)
        if written < 0:
            raise RuntimeError(self.lib.dr_liquid_error().decode())
        return out[:written]

    def demodulate(self, samples, reset=True):
        samples = np.ascontiguousarray(samples, dtype=np.complex64)
        length = self.lib.dr_liquid_receive(self.handle, samples.ctypes.data, samples.size,
                                          self.buffer.ctypes.data, self.buffer.size, int(reset))
        if length < 0:
            raise RuntimeError(self.lib.dr_liquid_error().decode())
        return self.buffer[:length].tobytes() if length else None

    def receive_many(self, samples, reset=False):
        """Return every CRC-verified packet completed in this streaming block."""
        samples = np.ascontiguousarray(samples, dtype=np.complex64)
        # At most one packet byte per input sample, plus conservative overhead.
        output = np.empty(max(65536, samples.size * 4 + 65536), dtype=np.uint8)
        count = self.lib.dr_liquid_receive_many(self.handle, samples.ctypes.data, samples.size,
                                                output.ctypes.data, output.size, int(reset))
        if count < 0:
            raise RuntimeError(self.lib.dr_liquid_error().decode())
        raw, packets, offset = output[:count].tobytes(), [], 0
        from .ofdma import Packet, _HEADER, _CRC, _MAGIC
        while offset < len(raw):
            length = struct.unpack_from("!I", raw, offset)[0]
            offset += 4
            body = raw[offset:offset + length]
            offset += length
            if len(body) < _HEADER.size + _CRC.size:
                continue
            magic, source, destination, sequence, size = _HEADER.unpack_from(body)
            end = _HEADER.size + size
            if (magic == _MAGIC and end + 4 == len(body) and
                    zlib.crc32(body[:end]) == _CRC.unpack_from(body, end)[0]):
                packets.append(Packet(source, destination, sequence, body[_HEADER.size:end]))
        return packets
