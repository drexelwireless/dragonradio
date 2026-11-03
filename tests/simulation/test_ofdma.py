"""Behavioral PHY checks, including isolation and expected failure cases."""
from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

from dragonradio.simulation import (Allocation, Channel, ChannelConfig, OFDMAConfig,
                                    OFDMAReceiver, OFDMATransmitter, Packet)
from dragonradio.simulation.ofdma import _waveform
from dragonradio.simulation.runner import channel_profiles, run_experiment


ROOT = Path(__file__).resolve().parents[2]


class OFDMATest(unittest.TestCase):
    def setUp(self):
        self.config = OFDMAConfig()
        self.tx = OFDMATransmitter(self.config)
        self.packets = (Packet(0, 1, 83, bytes(range(256))), Packet(0, 2, 83, b"user two" * 29))

    def test_noiseless_both_users_same_waveform(self):
        waveform = self.tx.modulate(self.packets)
        self.assertEqual(len(waveform), self.config.frame_samples)
        for expected in self.packets:
            result = OFDMAReceiver(expected.destination, self.config).demodulate(waveform)
            self.assertTrue(result.ok, result.error)
            self.assertEqual(result.packet, expected)
            self.assertEqual(result.start_sample, 0)

    def test_random_lengths_including_empty(self):
        rng = np.random.default_rng(51)
        for length in (0, 1, 43, 255, 256):
            packets = [Packet(7, i, length, rng.bytes(length)) for i in (1, 2)]
            waveform = self.tx.modulate(packets)
            for packet in packets:
                result = OFDMAReceiver(packet.destination).demodulate(waveform)
                self.assertEqual(result.packet, packet)

    def test_multipath_timing_and_cfo_estimates(self):
        waveform = self.tx.modulate(self.packets)
        for i, channel in enumerate(channel_profiles("multipath")):
            samples = Channel(channel, 17).propagate(waveform, self.config.nfft)
            result = OFDMAReceiver(i + 1).demodulate(samples)
            self.assertEqual(result.packet, self.packets[i], result.error)
            self.assertEqual(result.start_sample, channel.delay_samples)
            self.assertAlmostEqual(result.cfo_subcarriers, channel.cfo_subcarriers, delta=0.003)

    def test_distinct_user_payload_changes_do_not_affect_other_user(self):
        first = self.tx.modulate(self.packets)
        other = self.tx.modulate([self.packets[0], replace(self.packets[1], payload=b"different")])
        self.assertFalse(np.array_equal(first, other))
        receiver = OFDMAReceiver(1)
        self.assertEqual(receiver.demodulate(first).packet, receiver.demodulate(other).packet)

    def test_corruption_is_confined_to_allocated_user(self):
        grid = self.tx.build_grid(self.packets)
        # Flip QPSK symbols in the middle of user 1's packet, leaving training
        # and pilots intact. A demapper returning expected bytes would miss this.
        bins = np.array(self.config.allocations[0].data) % self.config.nfft
        grid[6, bins] *= -1
        waveform = _waveform(grid, self.config)
        broken = OFDMAReceiver(1).demodulate(waveform)
        self.assertFalse(broken.ok)
        self.assertEqual(broken.error, "payload CRC failed")
        self.assertEqual(OFDMAReceiver(2).demodulate(waveform).packet, self.packets[1])

    def test_dc_and_guard_bins_are_unused(self):
        grid = self.tx.build_grid(self.packets)
        occupied = {k % self.config.nfft for a in self.config.allocations for k in a.carriers}
        nulls = [k for k in range(self.config.nfft) if k not in occupied]
        np.testing.assert_array_equal(grid[:, nulls], 0)
        # Each user has 44 data carriers in every data symbol.
        self.assertEqual([len(a.data) for a in self.config.allocations], [44, 44])

    def test_silence_and_noise_are_not_packets(self):
        receiver = OFDMAReceiver(1)
        self.assertFalse(receiver.demodulate(np.zeros(self.config.frame_samples + 300)).ok)
        rng = np.random.default_rng(91)
        noise = rng.normal(size=5000) + 1j * rng.normal(size=5000)
        self.assertFalse(receiver.demodulate(noise).ok)

    def test_short_and_nan_inputs(self):
        for samples in (np.zeros(10), np.full(self.config.frame_samples, np.nan)):
            self.assertFalse(OFDMAReceiver(1).demodulate(samples).ok)

    def test_frequency_offset_outside_acquisition_range_fails(self):
        x = self.tx.modulate(self.packets)
        y = Channel(ChannelConfig(None, cfo_subcarriers=0.7)).propagate(x, self.config.nfft)
        self.assertFalse(OFDMAReceiver(1).demodulate(y).ok)

    def test_sample_channel_seed_reproducibility(self):
        x = self.tx.modulate(self.packets)
        config = ChannelConfig(12, delay_samples=21)
        np.testing.assert_array_equal(Channel(config, 5).propagate(x, 128), Channel(config, 5).propagate(x, 128))
        self.assertFalse(np.array_equal(Channel(config, 5).propagate(x, 128), Channel(config, 6).propagate(x, 128)))

    def test_unequal_allocations_and_capacity(self):
        cfg = OFDMAConfig(allocations=(Allocation(1, tuple(range(-40, -8)), (-40, -24)),
                                       Allocation(2, tuple(range(5, 53)), (5, 17, 29, 41))))
        x = OFDMATransmitter(cfg).modulate(self.packets)
        for packet in self.packets:
            self.assertEqual(OFDMAReceiver(packet.destination, cfg).demodulate(x).packet, packet)

    def test_invalid_allocations(self):
        base = self.config.allocations
        bad = (replace(base[1], carriers=base[0].carriers, pilots=base[0].pilots),
               replace(base[1], carriers=(0, 5, 6), pilots=(5, 6)),
               replace(base[1], pilots=(5, 5)), replace(base[1], user_id=1))
        for allocation in bad:
            with self.assertRaises(ValueError):
                OFDMAConfig(allocations=(base[0], allocation))

    def test_invalid_packets_and_configurations(self):
        for kwargs in ({"nfft": 32}, {"cp_len": 0}, {"sample_rate": -1}, {"max_payload": -1}):
            with self.assertRaises(ValueError):
                OFDMAConfig(**kwargs)
        with self.assertRaises(ValueError):
            self.tx.modulate([self.packets[0], self.packets[0]])
        with self.assertRaises(ValueError):
            self.tx.modulate([self.packets[0], replace(self.packets[1], source=3)])
        with self.assertRaises(ValueError):
            self.tx.modulate([self.packets[0], replace(self.packets[1], payload=b"x" * 257)])
        with self.assertRaises(ValueError):
            ChannelConfig(delay_samples=-1)
        with self.assertRaises(ValueError):
            ChannelConfig(taps=(0,))

    def test_empty_payload_capacity(self):
        cfg = OFDMAConfig(max_payload=0)
        packets = [Packet(0, i, 0, b"") for i in (1, 2)]
        x = OFDMATransmitter(cfg).modulate(packets)
        for packet in packets:
            self.assertEqual(OFDMAReceiver(packet.destination, cfg).demodulate(x).packet, packet)


class ExperimentTest(unittest.TestCase):
    def test_high_snr_acceptance(self):
        result = run_experiment(frames=30, channels=channel_profiles("multipath"))
        self.assertTrue(result["acceptance"]["passed"])
        self.assertEqual([u["delivered"] for u in result["users"]], [30, 30])

    def test_low_snr_failure_is_reported(self):
        result = run_experiment(frames=10, channels=channel_profiles("awgn", -5))
        self.assertFalse(result["acceptance"]["passed"])
        self.assertTrue(all(u["packet_error_rate"] > 0.9 for u in result["users"]))

    def test_seed_reproduces_results(self):
        kwargs = dict(frames=3, channels=channel_profiles("awgn", 8), seed=57)
        self.assertEqual(run_experiment(**kwargs), run_experiment(**kwargs))

    def test_saved_iq_can_be_independently_decoded(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "frame.npz"
            run_experiment(frames=1, channels=channel_profiles("multipath"), export_iq=target)
            with np.load(target, allow_pickle=False) as archive:
                for user in (1, 2):
                    result = OFDMAReceiver(user).demodulate(archive["rx_" + str(user)])
                    self.assertTrue(result.ok, result.error)
                    self.assertEqual(result.packet.payload, archive["payload_" + str(user)].tobytes())

    def test_cli_exit_codes_and_json(self):
        script = ROOT / "scripts" / "sim-radio.py"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            good = subprocess.run([sys.executable, str(script), "--profile", "ideal", "--frames", "2", "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stderr)
            self.assertTrue(json.loads(output.read_text())["acceptance"]["passed"])
            bad = subprocess.run([sys.executable, str(script), "--profile", "awgn", "--snr-db", "-5", "--frames", "2"], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 1, bad.stderr)
            invalid = subprocess.run([sys.executable, str(script), "--frames", "0"], capture_output=True, text=True)
            self.assertEqual(invalid.returncode, 2)

    def test_existing_radio_simulation_entry_point(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts" / "standalone-radio.py"),
                                 "--simulate", "--profile", "ideal", "--frames", "2"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2/2 verified", result.stdout)

    def test_native_backend_never_silently_substitutes_reference_phy(self):
        try:
            import _dragonradio
        except ImportError:
            with self.assertRaisesRegex(RuntimeError, "compiled DragonRadio"):
                run_experiment(frames=1, phy="liquid-ofdm")
        else:
            result = run_experiment(frames=2, channels=channel_profiles("ideal"), phy="liquid-ofdm")
            self.assertTrue(result["acceptance"]["passed"])


if __name__ == "__main__":
    unittest.main()
