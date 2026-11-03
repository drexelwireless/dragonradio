"""Measured comparisons: original reference, optimized engines, real liquidDSP."""
import argparse
import gc
from dataclasses import replace
import hashlib
import importlib
import json
from pathlib import Path
import platform
import sys
import time

import numpy as np

from .channel import Channel, ChannelConfig
from .fast import FastTransmitter, FastReceiver
from .liquid import LiquidOFDM
from .ofdma import OFDMAConfig, OFDMATransmitter, OFDMAReceiver, Packet, ofdm_config
from .runner import channel_profiles


def _timings(functions, iterations):
    # Interleave backends so CPU warm-up/load affects each, and exclude channel
    # generation, PHY construction, FFT planning, and report serialization.
    for fn in functions.values():
        for _ in range(30):
            fn()
    measurements = {key: [] for key in functions}
    gc_was_enabled = gc.isenabled()
    gc.disable()
    try:
        keys = list(functions)
        for i in range(iterations):
            for key in keys[i % len(keys):] + keys[:i % len(keys)]:
                start = time.perf_counter_ns()
                functions[key]()
                measurements[key].append((time.perf_counter_ns() - start) / 1000)
    finally:
        if gc_was_enabled:
            gc.enable()
    return {key: dict(p50_us=float(np.median(t)), p95_us=float(np.percentile(t, 95)),
                      p99_us=float(np.percentile(t, 99)), iterations=iterations)
            for key, t in measurements.items()}


def benchmark(frames=300, iterations=500, seed=2026, snrs=(10, 15, 20, 30), profiles=("awgn", "multipath-user1", "multipath-user2")):
    if frames < 1 or iterations < 30:
        raise ValueError("need frames >= 1 and timing iterations >= 30")
    root = Path(__file__).resolve().parents[4]
    # Frozen, checked-in copy of the implementation delivered before optimization.
    sys.path.insert(0, str(root / "tests"))
    old = importlib.import_module("reference_phy_v1.ofdma")
    cfg = OFDMAConfig()
    old_cfg = old.OFDMAConfig()
    packets = [Packet(0, a.user_id, 37, bytes(range(256))) for a in cfg.allocations]
    old_packets = [old.Packet(p.source, p.destination, p.sequence, p.payload) for p in packets]
    reference_tx, reference_rx = old.OFDMATransmitter(old_cfg), old.OFDMAReceiver(1, old_cfg)
    numpy_tx, numpy_rx = OFDMATransmitter(cfg), OFDMAReceiver(1, cfg)
    fast_tx, fast_rx = FastTransmitter(cfg), FastReceiver(1, cfg)
    original_iq, optimized_iq, fast_iq = reference_tx.modulate(old_packets), numpy_tx.modulate(packets), fast_tx.modulate(packets)
    waveform_error = float(np.max(np.abs(original_iq - fast_iq)))
    if not np.array_equal(original_iq, optimized_iq) or waveform_error > 2e-6:
        raise RuntimeError("optimized engines changed the reference waveform")
    samples = Channel(channel_profiles("multipath")[0], seed).propagate(original_iq, cfg.nfft)
    ofdma_timing = _timings({"reference_tx": lambda: reference_tx.modulate(old_packets),
                            "reference_rx": lambda: reference_rx.demodulate(samples),
                            "numpy_tx": lambda: numpy_tx.modulate(packets),
                            "numpy_rx": lambda: numpy_rx.demodulate(samples),
                            "native_tx": lambda: fast_tx.modulate(packets),
                            "native_rx": lambda: fast_rx.demodulate(samples)}, iterations)
    single = ofdm_config()
    p = packets[0]
    single_tx, single_rx = FastTransmitter(single), FastReceiver(1, single)
    liquid = LiquidOFDM(single)
    new_iq, liquid_iq = single_tx.modulate([p]), liquid.modulate(p)
    channel_cfg = channel_profiles("multipath")[0]
    new_samples = Channel(channel_cfg, seed).propagate(new_iq, single.nfft)
    liquid_samples = Channel(channel_cfg, seed).propagate(liquid_iq, single.nfft)
    if single_rx.demodulate(new_samples).packet != p or liquid.demodulate(liquid_samples) != p.encode():
        raise RuntimeError("a backend failed the matched baseline sanity check")
    ofdm_timing = _timings({"native_tx": lambda: single_tx.modulate([p]),
                           "native_rx": lambda: single_rx.demodulate(new_samples),
                           "liquid_tx": lambda: liquid.modulate(p),
                           "liquid_rx": lambda: liquid.demodulate(liquid_samples)}, iterations)
    # Identical received IQ is used for the two OFDMA decoders. OFDM/liquidDSP
    # have different framing, so compare equal payload, grid, modulation,
    # channel parameters, and time-domain SNR instead of identical waveforms.
    quality = []
    rng = np.random.default_rng(seed)
    for profile in profiles:
        if profile == "awgn":
            profile_channel = ChannelConfig()
        elif profile in ("multipath-user1", "multipath-user2"):
            profile_channel = channel_profiles("multipath")[0 if profile.endswith("1") else 1]
        else:
            raise ValueError("unknown benchmark profile")
        for snr in snrs:
            counts = {"reference": 0, "native_ofdma": 0, "native_ofdm": 0, "liquid_ofdm": 0}
            costs = {"native_tx": [], "native_rx": [], "liquid_tx": [], "liquid_rx": []}
            trials = []
            cfg_channel = replace(profile_channel, snr_db=snr)
            for sequence in range(frames):
                payload = rng.bytes(256)
                pair = [Packet(0, i, sequence, payload if i == 1 else rng.bytes(256)) for i in (1, 2)]
                encoded = fast_tx.modulate(pair)
                received = Channel(cfg_channel, seed + sequence + 1000).propagate(encoded, cfg.nfft)
                legacy = reference_rx.demodulate(received)
                expected = pair[0]
                counts["reference"] += int(legacy.ok and legacy.packet.payload == expected.payload and legacy.packet.sequence == sequence)
                counts["native_ofdma"] += int(fast_rx.demodulate(received).packet == expected)
                start = time.perf_counter_ns()
                waveform = single_tx.modulate([expected])
                costs["native_tx"].append((time.perf_counter_ns() - start) / 1000)
                received = Channel(cfg_channel, seed + sequence + 1000).propagate(waveform, single.nfft)
                start = time.perf_counter_ns()
                result = single_rx.demodulate(received)
                costs["native_rx"].append((time.perf_counter_ns() - start) / 1000)
                counts["native_ofdm"] += int(result.packet == expected)
                trials.append(result.correction_trials)
                start = time.perf_counter_ns()
                waveform = liquid.modulate(expected)
                costs["liquid_tx"].append((time.perf_counter_ns() - start) / 1000)
                received = Channel(cfg_channel, seed + sequence + 1000).propagate(waveform, single.nfft)
                start = time.perf_counter_ns()
                recovered = liquid.demodulate(received)
                costs["liquid_rx"].append((time.perf_counter_ns() - start) / 1000)
                counts["liquid_ofdm"] += int(recovered == expected.encode())
            quality.append(dict(profile=profile, snr_db=snr, frames=frames, delivered=counts,
                                per={k: (frames - v) / frames for k, v in counts.items()},
                                live_p50_us={k: float(np.median(v)) for k, v in costs.items()},
                                live_mean_us={k: float(np.mean(v)) for k, v in costs.items()},
                                verified_payload_cpu_goodput_bps={
                                    "native": counts["native_ofdm"] * 256 * 8 * 1e6 / (sum(costs["native_tx"]) + sum(costs["native_rx"])),
                                    "liquid": counts["liquid_ofdm"] * 256 * 8 * 1e6 / (sum(costs["liquid_tx"]) + sum(costs["liquid_rx"]))},
                                mean_crc_correction_trials=float(np.mean(trials))))
    median = lambda table, key: table[key]["p50_us"]
    old_total = median(ofdma_timing, "reference_tx") + median(ofdma_timing, "reference_rx")
    fast_total = median(ofdma_timing, "native_tx") + median(ofdma_timing, "native_rx")
    native_total = median(ofdm_timing, "native_tx") + median(ofdm_timing, "native_rx")
    liquid_total = median(ofdm_timing, "liquid_tx") + median(ofdm_timing, "liquid_rx")
    gate = dict(reference_waveform_preserved=waveform_error <= 2e-6,
                native_ofdma_not_slower_than_reference=fast_total <= old_total,
                native_ofdm_not_slower_than_liquid=native_total <= liquid_total,
                reference_delivery_not_regressed=all(q["delivered"]["native_ofdma"] >= q["delivered"]["reference"] for q in quality),
                matched_liquid_delivery_not_regressed=all(q["delivered"]["native_ofdm"] >= q["delivered"]["liquid_ofdm"] for q in quality),
                per_profile_verified_cpu_goodput_not_lower=all(q["verified_payload_cpu_goodput_bps"]["native"]
                    >= q["verified_payload_cpu_goodput_bps"]["liquid"] for q in quality))
    return dict(schema_version=1, machine=platform.platform(), architecture=platform.machine(), python=platform.python_version(),
                numpy=np.__version__, seed=seed, payload_bytes=256, nfft=128, cp_len=16, sample_rate=1e6,
                reference_sha256=hashlib.sha256((root / "tests/reference_phy_v1/ofdma.py").read_bytes()).hexdigest(),
                native_source_sha256=hashlib.sha256((root / "src/simulation/FastPHY.cc").read_bytes()).hexdigest(),
                optimized_python_source_sha256=hashlib.sha256((root / "python/dragonradio/dragonradio/simulation/ofdma.py").read_bytes()).hexdigest(),
                liquid_revision="d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb",
                coding="matched CRC32 and uncoded QPSK; production default FEC is not compared",
                quality_profiles=list(profiles),
                ofdma_timing=ofdma_timing, ofdm_timing=ofdm_timing,
                ofdma_speedup_over_reference=old_total / fast_total,
                ofdm_speedup_over_liquid=liquid_total / native_total,
                ofdm_samples=dict(native=len(new_iq), liquid=len(liquid_iq)),
                quality=quality, gates=gate, passed=all(gate.values()),
                limitations=["Different framing; the new static PHY has fewer preamble/header symbols.",
                             "liquid RX timing returns CRC-valid raw payload; new RX additionally parses and validates the packet envelope.",
                             "Warm engine timings exclude construction/FFT planning, channel generation and I/O.",
                             "CPU timing and finite-run PER describe this machine/configuration, not universal dominance.",
                             "No claim of equivalence to production FEC, all channels, or RF operation.",
                             "Early failed acquisition can be faster than successful decoding; per-profile gates use verified payload per measured CPU second."])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--iterations", type=int, default=500)
    parser.add_argument("--output", type=Path, default=Path("simulation-results/benchmark.json"))
    args = parser.parse_args(argv)
    result = benchmark(args.frames, args.iterations)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: result[k] for k in ("ofdma_speedup_over_reference", "ofdm_speedup_over_liquid", "quality", "gates", "passed")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
