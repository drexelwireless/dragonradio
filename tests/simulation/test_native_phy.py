from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest

import numpy as np

from dragonradio.simulation.channel import Channel, ChannelConfig
from dragonradio.simulation.fast import FastTransmitter, FastReceiver, library_path
from dragonradio.simulation.frame import FrameEngine, NativePacketAdapter, normalize_iq
from dragonradio.simulation.ofdma import OFDMAConfig, OFDMATransmitter, OFDMAReceiver, Packet, ofdm_config
from dragonradio.simulation.transport import SDRConfig, UHDTransport

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(library_path().exists(), "build native engine with scripts/build-phy.py")
class NativePHYTest(unittest.TestCase):
    def test_ofdm_and_ofdma_cross_decode_and_waveform_equivalence(self):
        for cfg in (ofdm_config(), OFDMAConfig()):
            packets = [Packet(0, a.user_id, 100 + a.user_id, bytes(range(256))) for a in cfg.allocations]
            native, reference = FastTransmitter(cfg), OFDMATransmitter(cfg)
            x, expected_iq = native.modulate(packets), reference.modulate(packets)
            np.testing.assert_allclose(x, expected_iq, atol=2e-6, rtol=2e-6)
            for p in packets:
                self.assertEqual(OFDMAReceiver(p.destination, cfg).demodulate(x).packet, p)
                self.assertEqual(FastReceiver(p.destination, cfg).demodulate(expected_iq).packet, p)

    def test_full_cp_channel_support(self):
        cfg = ofdm_config()
        p = Packet(0, 1, 0, bytes(range(256)))
        taps = tuple([1] + [0] * 14 + [0.2j])
        samples = Channel(ChannelConfig(30, taps, 19, -0.1), 99).propagate(FastTransmitter(cfg).modulate([p]), cfg.nfft)
        self.assertEqual(FastReceiver(1, cfg).demodulate(samples).packet, p)

    def test_stream_reassembly_across_variable_chunks(self):
        for cfg in (ofdm_config(), OFDMAConfig()):
            engine = FrameEngine(cfg, "native")
            decoder = engine.stream_receiver(1)
            expected, chunks = [], [np.zeros(713, dtype=np.complex64)]
            for i in range(5):
                packets = [Packet(0, a.user_id, 7 * i + a.user_id, b"frame " + bytes([i])) for a in cfg.allocations]
                expected.append(packets[0])
                frame = engine.build(packets)
                self.assertEqual(len(frame.records), len(packets))
                chunks.extend((frame.samples, np.zeros(37, dtype=np.complex64)))
            samples = np.concatenate(chunks)
            decoded = []
            for start in range(0, len(samples), 97):
                decoded.extend(decoder.feed(samples[start:start + 97]))
            decoded.extend(decoder.feed([], final=True))
            self.assertEqual([r.packet for r in decoded if r.ok], expected)
            self.assertLess(decoder.pending_samples, cfg.frame_samples)

    def test_stream_recovers_after_crc_failed_frame(self):
        cfg = ofdm_config()
        engine = FrameEngine(cfg, "native")
        packets = [Packet(0, 1, i, bytes(range(256))) for i in range(2)]
        broken = engine.build([packets[0]]).samples.copy()
        broken[4 * cfg.symbol_samples:8 * cfg.symbol_samples] *= -1
        stream = np.concatenate((broken, np.zeros(47), engine.build([packets[1]]).samples, np.zeros(300)))
        results = engine.stream_receiver(1).feed(stream, final=True)
        self.assertTrue(any(not r.ok for r in results))
        self.assertEqual([r.packet for r in results if r.ok], [packets[1]])

    def test_native_packet_adapter_preserves_flags_payload_and_independent_sequences(self):
        def header(source, destination, sequence):
            return SimpleNamespace(curhop=source, nexthop=destination, seq=sequence,
                                   flags=SimpleNamespace(syn=1, ack=0, has_seq=1, has_control=0, compressed=0, team=3))
        cfg = OFDMAConfig(max_payload=257)
        adapter = NativePacketAdapter(FrameEngine(cfg, "native"))
        original = [SimpleNamespace(hdr=header(7, i, 15 + 3 * i), payload=bytes(range(256))) for i in (1, 2)]
        frame = adapter.build(original)
        radio = SimpleNamespace(Header=header, RadioPacket=lambda h, p: SimpleNamespace(hdr=h, payload=p))
        for p in original:
            result, received = adapter.receive(p.hdr.nexthop, frame.samples, radio)
            self.assertTrue(result.ok)
            self.assertEqual(received.payload, p.payload)
            self.assertEqual(received.hdr.seq, p.hdr.seq)
            self.assertEqual(vars(received.hdr.flags), vars(p.hdr.flags))

    def test_silence_nan_short_and_corrupted_frame(self):
        cfg = ofdm_config()
        receiver = FastReceiver(1, cfg)
        for samples in (np.zeros(cfg.frame_samples + 500), np.zeros(20), np.full(cfg.frame_samples, np.nan)):
            self.assertFalse(receiver.demodulate(samples).ok)


class UHDTransportContractTest(unittest.TestCase):
    """Mock driver verifies API protocol, not hardware or RF performance."""
    def make_driver(self, error=False):
        calls = []
        class Stream:
            def send(self, samples, md, timeout):
                calls.append((samples.copy(), md.start_of_burst, md.end_of_burst, md.has_time_spec))
                return min(37, samples.shape[1])
            def recv_async_msg(self, md, timeout):
                md.event_code = "underflow" if error else "ack"
                return True
        class Device:
            def __init__(self, args):
                pass
            def __getattr__(self, name):
                if name.startswith("set_"):
                    return lambda *args: None
                raise AttributeError(name)
            def get_tx_rate(self, ch):
                return 1e6
            def get_tx_stream(self, args):
                return Stream()
            def get_time_now(self):
                return 0.0
        module = SimpleNamespace(usrp=SimpleNamespace(MultiUSRP=Device, StreamArgs=lambda *args: SimpleNamespace()),
                                 types=SimpleNamespace(TXMetadata=SimpleNamespace, TXAsyncMetadata=SimpleNamespace,
                                                       TimeSpec=float, TXMetadataEventCode=SimpleNamespace(burst_ack="ack")))
        return module, calls

    def test_partial_tx_start_end_metadata_and_scaling(self):
        module, calls = self.make_driver()
        cfg = SDRConfig("addr=test", 1e9)
        transport = UHDTransport(cfg, "tx", module)
        frame = SimpleNamespace(sample_rate=1e6, samples=np.full(100, 3 + 4j, dtype=np.complex64))
        result = transport.transmit(frame)
        self.assertEqual(result["samples"], 100)
        self.assertLessEqual(result["peak"], 0.500001)
        self.assertEqual(calls[0][1:], (True, False, True))
        self.assertEqual(calls[1][1:], (False, False, False))
        self.assertEqual(calls[-1][1:], (False, True, False))

    def test_invalid_sdr_config(self):
        for kwargs in ({"device_args": "", "center_frequency": 1e9}, {"device_args": "addr=test", "center_frequency": None}):
            with self.assertRaises(ValueError):
                SDRConfig(**kwargs)

    def test_async_tx_failure_is_reported(self):
        module, calls = self.make_driver(error=True)
        transport = UHDTransport(SDRConfig("addr=test", 1e9), "tx", module)
        frame = SimpleNamespace(sample_rate=1e6, samples=np.ones(100, dtype=np.complex64))
        with self.assertRaisesRegex(RuntimeError, "underflow"):
            transport.transmit(frame)

    def test_mode_ofdm_cli(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/sim-radio.py"), "--phy", "ofdm", "--frames", "2", "--profile", "ideal"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2/2 verified", result.stdout)

    def test_real_liquid_modem_when_built(self):
        from dragonradio.simulation.liquid import LiquidOFDM
        cfg = ofdm_config()
        try:
            modem = LiquidOFDM(cfg)
        except RuntimeError as exc:
            if "bridge absent" in str(exc):
                self.skipTest("build liquidDSP modem bridge")
            raise
        packet = Packet(0, 1, 87, bytes(range(256)))
        waveform = modem.modulate(packet)
        received = Channel(ChannelConfig(30, (1, 0.15j), 27, 0.1), 51).propagate(waveform, cfg.nfft)
        self.assertEqual(modem.demodulate(received), packet.encode())

    @unittest.skipUnless(library_path().exists(), "native library required")
    def test_benchmark_report_schema_with_real_backends(self):
        from dragonradio.simulation.benchmark import benchmark
        try:
            report = benchmark(frames=2, iterations=30, snrs=(30,), profiles=("awgn",))
        except RuntimeError as exc:
            if "bridge absent" in str(exc):
                self.skipTest("liquid modem bridge required")
            raise
        self.assertEqual(len(report["quality"]), 1)
        self.assertGreater(report["quality"][0]["verified_payload_cpu_goodput_bps"]["native"], 0)
        self.assertIn("per_profile_verified_cpu_goodput_not_lower", report["gates"])

    def test_linear_normalization_preserves_relative_samples(self):
        samples = np.array([2 + 1j, -1j, 0], dtype=np.complex64)
        scaled, factor = normalize_iq(samples)
        np.testing.assert_allclose(scaled, samples * factor)
        self.assertLessEqual(float(np.max(np.abs(scaled))), 0.500001)

    def test_uhd_dry_run_does_not_import_driver(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/frame-radio.py"), "--transport", "uhd", "--role", "tx", "--dry-run"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"hardware_opened": false', result.stdout)


if __name__ == "__main__":
    unittest.main()
