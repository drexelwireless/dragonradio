"""Native engine v2: CFO-robust acquisition, 16-QAM, Chase switch, C++ stream."""
import unittest

import numpy as np

from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.fast import FastReceiver, FastTransmitter, NativeStream, library_path
from dragonradio.simulation.frame import FrameEngine, StreamReceiver
from dragonradio.simulation.ofdma import OFDMAConfig, OFDMAReceiver, OFDMATransmitter, Packet, ofdm_config

V1 = library_path("reference-v1")


def packets(cfg, seed, sequence=0):
    rng = np.random.default_rng(seed)
    return [Packet(0, a.user_id, sequence, rng.bytes(int(rng.integers(0, cfg.max_payload + 1)))) for a in cfg.allocations]


@unittest.skipUnless(library_path().exists(), "build native engine with scripts/build-phy.py")
class NativeV2Test(unittest.TestCase):
    @unittest.skipUnless(V1.exists(), "build the frozen v1 engine with scripts/build-phy.py --variant reference-v1")
    def test_qpsk_transmit_is_byte_identical_to_v1(self):
        for cfg in (ofdm_config(), OFDMAConfig(), ofdm_config(max_payload=64)):
            new, old = FastTransmitter(cfg), FastTransmitter(cfg, library=V1)
            for seed in range(5):
                p = packets(cfg, seed, seed)
                self.assertEqual(new.modulate(p).tobytes(), old.modulate(p).tobytes())

    def test_v1_rejects_unsupported_options(self):
        if not V1.exists():
            self.skipTest("v1 engine not built")
        with self.assertRaises(ValueError):
            FastReceiver(1, ofdm_config(modulation="16qam"), library=V1)
        with self.assertRaises(ValueError):
            FastReceiver(1, ofdm_config(), library=V1, chase_bits=0)

    def test_16qam_matches_reference_and_cross_decodes(self):
        for cfg in (ofdm_config(modulation="16qam"), OFDMAConfig(modulation="16qam")):
            self.assertEqual(cfg.bits_per_symbol, 4)
            p = packets(cfg, 7, 9)
            x, ref = FastTransmitter(cfg).modulate(p), OFDMATransmitter(cfg).modulate(p)
            np.testing.assert_allclose(x, ref, atol=2e-6, rtol=2e-6)
            for q in p:
                self.assertEqual(OFDMAReceiver(q.destination, cfg).demodulate(x).packet, q)
                y = Channel(ChannelConfig(30, (1, .3j, 0, .15), 19, -0.4), 5).propagate(x, cfg.nfft)
                self.assertEqual(FastReceiver(q.destination, cfg).demodulate(y).packet, q)

    def test_16qam_halves_frame_airtime(self):
        self.assertLess(ofdm_config(modulation="16qam").frame_samples, 0.62 * ofdm_config().frame_samples)
        with self.assertRaises(ValueError):
            ofdm_config(modulation="64qam")

    def test_cfo_acquisition_range(self):
        for cfg in (ofdm_config(), OFDMAConfig()):
            tx = FastTransmitter(cfg)
            receivers = {a.user_id: FastReceiver(a.user_id, cfg) for a in cfg.allocations}
            for cfo in (-1.2, -1.0, -0.6, -0.3, 0.3, 0.6, 1.0, 1.2):
                p = packets(cfg, 11, int(abs(cfo) * 10))
                y = Channel(ChannelConfig(None, delay_samples=31, cfo_subcarriers=cfo), 3).propagate(tx.modulate(p), cfg.nfft)
                for q in p:
                    d = receivers[q.destination].demodulate(y)
                    self.assertEqual(d.packet, q, (cfo, d.error))
                    self.assertAlmostEqual(d.cfo_subcarriers, cfo, delta=0.01)
                    self.assertEqual(d.start_sample, 31)

    def test_chase_switch(self):
        cfg = ofdm_config()
        tx, rx0 = FastTransmitter(cfg), FastReceiver(1, cfg, chase_bits=0)
        p = packets(cfg, 3)
        d = rx0.demodulate(Channel(ChannelConfig(30, delay_samples=5), 1).propagate(tx.modulate(p), cfg.nfft))
        self.assertEqual(d.packet, p[0])
        self.assertEqual(d.correction_trials, 0)
        with self.assertRaises(RuntimeError):
            FastReceiver(1, cfg, chase_bits=9)

    def test_noise_and_silence_are_never_packets(self):
        cfg = ofdm_config()
        rx = FastReceiver(1, cfg)
        rng = np.random.default_rng(1)
        for scale in (0.0, 1e-3, 1.0):
            y = (scale * (rng.standard_normal(20000) + 1j * rng.standard_normal(20000))).astype(np.complex64)
            self.assertFalse(rx.demodulate(y).ok)
            stream = NativeStream(rx)
            self.assertFalse(any(r.ok for r in stream.feed(y, final=True)))


@unittest.skipUnless(library_path().exists(), "build native engine with scripts/build-phy.py")
class NativeStreamTest(unittest.TestCase):
    def corpus(self, cfg, frames, seed, idle=50000, gap=64, snr=None, cfo=0.0):
        engine = FrameEngine(cfg, "native")
        rng = np.random.default_rng(seed)
        expected = {a.user_id: [] for a in cfg.allocations}
        pieces = [((rng.standard_normal(idle) + 1j * rng.standard_normal(idle)) * 1e-3).astype(np.complex64)]
        for i in range(frames):
            p = [Packet(0, a.user_id, i, rng.bytes(cfg.max_payload)) for a in cfg.allocations]
            for q in p:
                expected[q.destination].append(q)
            pieces += [engine.build(p).samples, np.zeros(gap + int(rng.integers(0, 40)), np.complex64)]
        pieces.append(np.zeros(idle, np.complex64))
        x = np.concatenate(pieces)
        if snr is not None or cfo:
            x = Channel(ChannelConfig(snr, delay_samples=0, cfo_subcarriers=cfo), seed).propagate(x, cfg.nfft, 0)
        return x, expected

    def run_stream(self, receiver, x, chunk):
        results = []
        for i in range(0, len(x), chunk):
            results.extend(receiver.feed(x[i:i + chunk]))
        results.extend(receiver.feed([], final=True))
        return [r for r in results if r.ok]

    def test_matches_python_stream_across_chunks_and_compaction(self):
        for cfg in (ofdm_config(), OFDMAConfig(), ofdm_config(modulation="16qam")):
            x, expected = self.corpus(cfg, 120, 4)
            self.assertGreater(len(x), 3 * 65536)   # crosses buffer compaction
            for user in expected:
                engine = FrameEngine(cfg, "native")
                reference = [(r.packet, r.start_sample) for r in self.run_stream(StreamReceiver(engine.receivers[user], cfg), x, 4096)]
                self.assertEqual([p for p, _ in reference], expected[user])
                for chunk in (97, 4096, 65536, len(x)):
                    stream = engine.stream_receiver(user)
                    self.assertIsInstance(stream, NativeStream)
                    got = [(r.packet, r.start_sample) for r in self.run_stream(stream, x, chunk)]
                    self.assertEqual(got, reference, (cfg.modulation, user, chunk))
                    self.assertLess(stream.pending_samples, cfg.frame_samples)

    def test_noisy_cfo_stream(self):
        cfg = OFDMAConfig()
        x, expected = self.corpus(cfg, 20, 9, idle=20000, snr=25, cfo=-0.9)
        engine = FrameEngine(cfg, "native")
        for user in expected:
            got = [r.packet for r in self.run_stream(engine.stream_receiver(user), x, 4096)]
            self.assertEqual(got, expected[user])

    def test_recovers_after_corrupted_frame_and_reset(self):
        cfg = ofdm_config()
        engine = FrameEngine(cfg, "native")
        p = [Packet(0, 1, i, bytes([i]) * 200) for i in range(3)]
        broken = engine.build([p[0]]).samples.copy()
        broken[600:900] = 0
        x = np.concatenate([np.zeros(500, np.complex64), broken, np.zeros(80, np.complex64),
                            engine.build([p[1]]).samples, np.zeros(80, np.complex64), engine.build([p[2]]).samples])
        stream = engine.stream_receiver(1)
        self.assertEqual([r.packet for r in self.run_stream(stream, x, 333)], p[1:])
        stream.reset()
        self.assertEqual([r.packet for r in self.run_stream(stream, x, len(x))], p[1:])

    def test_requires_v2_engine(self):
        if not V1.exists():
            self.skipTest("v1 engine not built")
        engine = FrameEngine(ofdm_config(), "native", library=V1)
        self.assertIsInstance(engine.stream_receiver(1), StreamReceiver)
        with self.assertRaises(ValueError):
            FrameEngine(ofdm_config(), "native", library=V1, stream="native").stream_receiver(1)


if __name__ == "__main__":
    unittest.main()
