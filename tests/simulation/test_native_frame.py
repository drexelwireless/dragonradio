"""Framed native OFDM (wire format v2) used by the production adapter."""
import struct
import unittest
import zlib

import numpy as np

from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.fast import library_path


def _available():
    try:
        from dragonradio.simulation.framed import FramedModem
        FramedModem(max_payload=16).close()
        return True
    except Exception:
        return False


def prbs15(seed, count):
    state = (seed & 0x7fff) or 1
    out = np.empty(count, np.uint8)
    for i in range(count):
        b = ((state >> 14) ^ (state >> 13)) & 1
        state = ((state << 1) | b) & 0x7fff
        out[i] = b
    return out


def reference_frame(info, payload, mcsidx, bps, data, pilots, nfft=128, cp=16, user=1, max_payload=2048):
    """Independent NumPy transmitter written from the NativeFrame.hh format text."""
    nd = len(data)
    H = -(-112 // (2 * nd))
    P = -(-((len(payload) + 4) * 8) // (bps * nd))
    header = bytes([0xD2]) + bytes(info) + bytes([mcsidx, bps]) + struct.pack(">H", len(payload))
    header += struct.pack(">I", zlib.crc32(header))
    body = bytes(payload) + struct.pack(">I", zlib.crc32(bytes(payload)))
    signed = lambda k: k - nfft if k >= nfft // 2 else k
    active = sorted(list(data) + list(pilots), key=signed)
    training = np.zeros(nfft, complex)
    training[active] = 1 - 2 * prbs15(0x1921, len(active)).astype(float)
    max_symbols = H + -(-((max_payload + 4) * 8) // (2 * nd))
    scramble = prbs15(0x2A00 + user, 4 * max_symbols * nd)
    hbits = np.zeros(2 * H * nd, np.uint8)
    hbits[:112] = np.unpackbits(np.frombuffer(header, np.uint8))
    pbits = np.zeros(bps * P * nd, np.uint8)
    pbits[:len(body) * 8] = np.unpackbits(np.frombuffer(body, np.uint8))
    hbits ^= scramble[:hbits.size]
    pbits ^= scramble[hbits.size:hbits.size + pbits.size]
    q = 1 / np.sqrt(2)
    a16 = 1 / np.sqrt(10)
    grid = np.zeros((2 + H + P, nfft), complex)
    grid[:2] = training
    for d in range(H + P):
        if d < H:
            b = hbits[2 * d * nd:2 * (d + 1) * nd].reshape(-1, 2).astype(float)
            grid[2 + d, data] = ((1 - 2 * b[:, 0]) + 1j * (1 - 2 * b[:, 1])) * q
        else:
            b = pbits[bps * (d - H) * nd:bps * (d - H + 1) * nd].reshape(-1, bps).astype(float)
            if bps == 2:
                grid[2 + d, data] = ((1 - 2 * b[:, 0]) + 1j * (1 - 2 * b[:, 1])) * q
            else:
                grid[2 + d, data] = ((1 - 2 * b[:, 0]) * (1 + 2 * b[:, 1]) + 1j * (1 - 2 * b[:, 2]) * (1 + 2 * b[:, 3])) * a16
        grid[2 + d, pilots] = [(-1.0 if (d + j + user) % 2 else 1.0) for j in range(len(pilots))]
    td = np.fft.ifft(grid, axis=1, norm="ortho")
    return np.concatenate((td[:, -cp:], td), axis=1).reshape(-1)


@unittest.skipUnless(library_path().exists() and _available(), "build native engine with scripts/build-phy.py")
class NativeFrameTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from dragonradio.simulation.framed import FramedModem, default_allocation
        cls.FramedModem = FramedModem
        cls.data, cls.pilots = default_allocation()
        cls.modem = FramedModem(max_payload=2048)

    def frames(self, n, seed, mixed=True):
        rng = np.random.default_rng(seed)
        out = []
        for i in range(n):
            length = int(rng.integers(0, 1600))
            bps = int(rng.choice([2, 4])) if mixed else 2
            info = bytes([int(rng.integers(0, 254)), int(rng.integers(0, 255)), i & 255, i >> 8, int(rng.integers(0, 256))])
            out.append((info, rng.bytes(length), int(rng.integers(0, 8)), bps))
        return out

    def run_stream(self, stream, x, chunk):
        results = []
        for i in range(0, len(x), chunk):
            results.extend(stream.feed(x[i:i + chunk]))
        results.extend(stream.feed([], final=True))
        return results

    def test_transmitter_matches_independent_reference(self):
        for info, payload, mcs, bps in self.frames(12, 1) + [(b"\x01\x02\x03\x04\x05", b"", 0, 2), (b"\xff" * 5, bytes(2048), 7, 4)]:
            x = self.modem.modulate(info, payload, mcs, bps)
            ref = reference_frame(info, payload, mcs, bps, self.data, self.pilots)
            self.assertEqual(x.size, ref.size)
            np.testing.assert_allclose(x, ref, atol=3e-6, rtol=0)

    def test_header_fits_one_symbol_and_lengths_scale(self):
        self.assertEqual(self.modem.header_symbols, 1)
        self.assertEqual(self.modem.frame_samples(0, 2), 4 * 144)
        self.assertLess(self.modem.frame_samples(1500, 4), 0.55 * self.modem.frame_samples(1500, 2))
        with self.assertRaises(ValueError):
            self.modem.modulate(b"12345", bytes(2049))
        with self.assertRaises(ValueError):
            self.modem.modulate(b"12345", b"x", 0, 6)

    def test_stream_delivers_mixed_frames_across_chunks_and_impairments(self):
        frames = self.frames(25, 2)
        rng = np.random.default_rng(3)
        pieces = [np.zeros(3000, np.complex64)]
        for f in frames:
            pieces += [self.modem.modulate(*f), np.zeros(int(rng.integers(0, 300)), np.complex64)]
        pieces.append(np.zeros(2000, np.complex64))
        clean = np.concatenate(pieces)
        for cfg in (ChannelConfig(None), ChannelConfig(32, (1, .3j, 0, .15), 0, -0.7)):
            x = Channel(cfg, 5).propagate(clean, 128, 0)
            for chunk in (101, 4096, len(x)):
                got = [r for r in self.run_stream(self.modem.stream(), x, chunk) if r.ok]
                self.assertEqual([(r.info, r.payload, r.mcsidx, r.bits_per_symbol) for r in got], frames, (cfg, chunk))
                self.assertTrue(all(r.chase_trials == 0 or r.ok for r in got))

    def test_unwanted_frames_are_skipped_without_payload_decode(self):
        frames = [(bytes([1, nh, i, 0, 0]), bytes([i]) * 300, 0, 2) for i, nh in enumerate([7, 9, 255, 7, 3])]
        x = np.concatenate([np.concatenate((self.modem.modulate(*f), np.zeros(50, np.complex64))) for f in frames])
        results = self.run_stream(self.modem.stream(nexthop_filter=7), x, 4096)
        self.assertEqual([r.info[1] for r in results], [7, 9, 255, 7, 3])
        self.assertEqual([r.wanted for r in results], [True, False, True, True, False])
        self.assertEqual([r.payload for r in results if r.ok], [f[1] for f in frames if f[0][1] in (7, 255)])
        self.assertTrue(all(r.payload == b"" and r.samples > 0 for r in results if not r.wanted))

    def test_skipping_unwanted_frames_across_compaction(self):
        # Unwanted long frames are skipped before their tail arrives; with small
        # chunks the skip target lies beyond the buffer when it compacts.
        frames = [(bytes([1, 9 if i % 3 else 7, i, 0, 0]), bytes([i]) * 1500, 0, 2) for i in range(60)]
        x = np.concatenate([np.concatenate((self.modem.modulate(*f), np.zeros(70, np.complex64))) for f in frames])
        self.assertGreater(len(x), 4 * 65536)
        for chunk in (97, 1000):
            stream = self.modem.stream(nexthop_filter=7)
            results = self.run_stream(stream, x, chunk)
            self.assertEqual([r.payload for r in results if r.ok], [f[1] for f in frames if f[0][1] == 7])
            self.assertEqual(sum(1 for r in results if r.header_valid and not r.wanted), 40)
            self.assertLess(stream.pending_samples, 70000)

    def test_corrupted_payload_consumes_frame_and_recovers(self):
        f = [(b"\x01\x02\x00\x00\x00", bytes(range(200)), 1, 2), (b"\x01\x02\x01\x00\x00", bytes(range(100)), 1, 4)]
        bad = self.modem.modulate(*f[0])
        bad[1000:1100] = 0
        x = np.concatenate([np.zeros(400, np.complex64), bad, np.zeros(60, np.complex64), self.modem.modulate(*f[1])])
        results = self.run_stream(self.modem.stream(), x, 512)
        self.assertEqual(len(results), 2)
        self.assertTrue(results[0].header_valid and not results[0].payload_valid)
        self.assertTrue(results[1].ok and results[1].payload == f[1][1])
        self.assertEqual(results[1].start - results[0].start, bad.size + 60)

    def test_noise_and_silence_produce_no_valid_headers(self):
        rng = np.random.default_rng(9)
        for scale in (0.0, 1e-3, 1.0):
            y = (scale * (rng.standard_normal(300000) + 1j * rng.standard_normal(300000))).astype(np.complex64)
            self.assertFalse(any(r.header_valid for r in self.run_stream(self.modem.stream(), y, 8192)))

    def test_bounded_buffer_and_frame_open(self):
        stream = self.modem.stream()
        x = self.modem.modulate(b"\x01\x02\x03\x00\x00", bytes(1500), 0, 2)
        stream.feed(np.zeros(1000, np.complex64))
        stream.feed(x[:2000])
        self.assertTrue(stream.frame_open)
        got = stream.feed(np.concatenate((x[2000:], np.zeros(800, np.complex64))))
        self.assertEqual([r.payload for r in got if r.ok], [bytes(1500)])
        stream.feed(np.zeros(300000, np.complex64))
        self.assertLess(stream.pending_samples, 70000)


if __name__ == "__main__":
    unittest.main()
