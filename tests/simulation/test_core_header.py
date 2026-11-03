"""The native engine core header is usable from C++ without the ctypes ABI.

Guards the P1 extraction: two translation units include FastPHYCore.hh (as a
production PHY adapter will), share one FFTW planner lock, and produce a frame
bit-identical to the ctypes library used by every other simulation test.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

import numpy as np

from dragonradio.simulation.fast import FastTransmitter, library_path
from dragonradio.simulation.ofdma import OFDMAConfig, Packet, _scramble, _training, ofdm_config

ROOT = Path(__file__).resolve().parents[2]
FFTW = Path(os.environ.get("FFTW_PREFIX", "/opt/homebrew" if platform.system() == "Darwin" else "/usr"))
CXX = os.environ.get("CXX", "c++")


@unittest.skipUnless(library_path().exists() and shutil.which(CXX) and (FFTW / "include" / "fftw3.h").exists(),
                     "needs the built native library, a C++17 compiler and FFTW headers")
class CoreHeaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / "core_header_check"
        cpp = ROOT / "tests" / "simulation" / "cpp"
        subprocess.run([CXX, "-std=c++17", "-O1", "-Wall", "-Werror", "-I" + str(ROOT / "src"),
                        "-I" + str(FFTW / "include"), str(cpp / "core_unit_a.cc"), str(cpp / "core_unit_b.cc"),
                        "-L" + str(FFTW / "lib"), "-lfftw3f", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def frame(self, cfg, packets):
        allocations = cfg.allocations
        encoded = [next(p for p in packets if p.destination == a.user_id).encode() for a in allocations]
        parts = [np.array([cfg.nfft, cfg.cp_len, cfg.data_symbols, len(allocations)], np.int32),
                 np.array([a.user_id for a in allocations], np.int32),
                 np.array([len(a.data) for a in allocations], np.int32),
                 np.array([len(a.pilots) for a in allocations], np.int32),
                 np.array([k % cfg.nfft for a in allocations for k in a.data], np.int32),
                 np.array([k % cfg.nfft for a in allocations for k in a.pilots], np.int32),
                 _training(cfg).astype(np.complex64),
                 np.concatenate([_scramble(np.zeros(2 * cfg.data_symbols * len(a.data), np.uint8), a.user_id)
                                 for a in allocations]),
                 np.array([len(e) for e in encoded], np.int32),
                 np.frombuffer(b"".join(encoded), np.uint8)]
        result = subprocess.run([str(self.exe)], input=b"".join(p.tobytes() for p in parts),
                                stdout=subprocess.PIPE, check=True)
        return np.frombuffer(result.stdout, dtype=np.complex64)

    def test_direct_core_matches_ctypes_library(self):
        rng = np.random.default_rng(5)
        for cfg in (ofdm_config(), OFDMAConfig(), ofdm_config(max_payload=64)):
            for sequence in range(3):
                packets = [Packet(0, a.user_id, sequence, rng.bytes(int(rng.integers(0, cfg.max_payload + 1))))
                           for a in cfg.allocations]
                direct = self.frame(cfg, packets)
                expected = FastTransmitter(cfg).modulate(packets)
                self.assertEqual(direct.size, cfg.frame_samples)
                self.assertEqual(direct.tobytes(), expected.tobytes())


if __name__ == "__main__":
    unittest.main()
