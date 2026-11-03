"""Reproducible simulation experiments and machine-readable acceptance results."""

from dataclasses import asdict, replace
import platform
from pathlib import Path
import subprocess

import numpy as np

from .channel import Channel, ChannelConfig
from .ofdma import OFDMAConfig, OFDMAReceiver, OFDMATransmitter, Packet


def channel_profiles(profile, snr_db=None, cfo=None, delays=None):
    if profile == "ideal":
        channels = [ChannelConfig(snr_db=None), ChannelConfig(snr_db=None)]
    elif profile == "awgn":
        channels = [ChannelConfig(snr_db=30), ChannelConfig(snr_db=30)]
    elif profile == "multipath":
        channels = [ChannelConfig(30, (1, 0.20 + 0.10j, 0, 0.08j), 17, 0.12, 0.4),
                    ChannelConfig(30, (0.8 + 0.3j, 0, 0.15 - 0.12j, 0, 0.05), 31, -0.18, -0.9)]
    else:
        raise ValueError("unknown channel profile")
    return tuple(replace(ch, snr_db=ch.snr_db if snr_db is None else snr_db,
                         cfo_subcarriers=ch.cfo_subcarriers if cfo is None else cfo[i],
                         delay_samples=ch.delay_samples if delays is None else delays[i])
                 for i, ch in enumerate(channels))


def _serializable_channels(channels):
    return [dict(asdict(ch), taps=[[complex(t).real, complex(t).imag] for t in ch.taps]) for ch in channels]


def _user_stats(user):
    return dict(user_id=user, attempted=0, delivered=0, crc_valid=0, wrong_packets=0,
                failures={}, evm_db=[], cfo_error_subcarriers=[], timing_error_samples=[],
                crc_correction_trials=0, frames_with_correction_search=0)


def _finish(stats):
    stats["packet_error_rate"] = 1 - stats["delivered"] / stats["attempted"]
    for key in ("evm_db", "cfo_error_subcarriers", "timing_error_samples"):
        values = stats.pop(key)
        stats["mean_" + key] = float(np.mean(values)) if values else None
        if key != "evm_db":
            stats["max_abs_" + key] = float(np.max(np.abs(values))) if values else None
    return stats


def run_experiment(config=OFDMAConfig(), frames=100, seed=2026,
                   channels=(ChannelConfig(), ChannelConfig()), payload_bytes=256,
                   max_per=0.01, phy="ofdma", export_iq=None, implementation="auto"):
    if frames < 1 or not 0 <= payload_bytes <= config.max_payload or not 0 <= max_per <= 1:
        raise ValueError("invalid frame count, payload length, or acceptance threshold")
    if len(channels) < len(config.allocations):
        raise ValueError("one channel per configured receiver is required")
    channels = channels[:len(config.allocations)]
    if phy not in ("ofdm", "ofdma", "liquid-ofdm"):
        raise ValueError("unknown PHY")
    if phy == "liquid-ofdm" and export_iq:
        raise ValueError("IQ export currently supports only the composite OFDMA waveform")
    payload_rng = np.random.default_rng(seed)
    # Separate RNG streams: receiver noise never changes transmitter payloads.
    paths = [Channel(ch, seed + 1000 + i) for i, ch in enumerate(channels)]
    stats = {a.user_id: _user_stats(a.user_id) for a in config.allocations}
    if phy in ("ofdm", "ofdma"):
        from .fast import make_phy
        tx, receivers, selected_engine = make_phy(config, implementation)
    else:
        from .native import NativeLiquidOFDM
        if tuple(a.user_id for a in config.allocations) != (1, 2):
            raise ValueError("native baseline uses users 1 and 2")
        native = NativeLiquidOFDM(config.nfft, config.cp_len)
        selected_engine = "embedded-liquidDSP"
    total_samples = 0
    worst_papr = 0.0
    for sequence in range(frames):
        packets = [Packet(0, a.user_id, sequence, payload_rng.bytes(payload_bytes)) for a in config.allocations]
        if phy in ("ofdm", "ofdma"):
            waveform = tx.modulate(packets)
            total_samples += len(waveform)
            worst_papr = max(worst_papr, float(10 * np.log10(np.max(np.abs(waveform) ** 2) / np.mean(np.abs(waveform) ** 2))))
            saved = {"tx": waveform}
        for index, packet in enumerate(packets):
            user = stats[packet.destination]
            user["attempted"] += 1
            if phy == "liquid-ofdm":
                waveform = native.modulate(packet)
                total_samples += len(waveform)
            samples = paths[index].propagate(waveform, config.nfft)
            if phy in ("ofdm", "ofdma"):
                result = receivers[packet.destination].demodulate(samples)
                user["crc_correction_trials"] += result.correction_trials
                user["frames_with_correction_search"] += int(result.correction_trials > 0)
                valid, exact = result.ok, result.packet == packet
                if result.evm_db is not None:
                    user["evm_db"].append(result.evm_db)
                if result.cfo_subcarriers is not None:
                    user["cfo_error_subcarriers"].append(result.cfo_subcarriers - channels[index].cfo_subcarriers)
                if result.start_sample is not None:
                    user["timing_error_samples"].append(result.start_sample - channels[index].delay_samples)
                error = result.error or "wrong packet"
                if sequence == 0:
                    saved["rx_" + str(packet.destination)] = samples
                    saved["payload_" + str(packet.destination)] = np.frombuffer(packet.payload, dtype=np.uint8)
            else:
                exact, count = native.receive(packet.destination, samples, packet)
                valid = bool(count)
                error = "native frame missing or mismatched"
            user["crc_valid"] += int(valid)
            user["delivered"] += int(exact)
            user["wrong_packets"] += int(valid and not exact)
            if not exact:
                user["failures"][error] = user["failures"].get(error, 0) + 1
        if phy in ("ofdm", "ofdma") and sequence == 0 and export_iq:
            target = Path(export_iq)
            target.parent.mkdir(parents=True, exist_ok=True)
            # Store configuration separately in the JSON experiment report.
            with target.open("wb") as stream:
                np.savez_compressed(stream, **saved)
    users = [_finish(u) for u in stats.values()]
    duration = total_samples / config.sample_rate
    passed = all(u["attempted"] - u["delivered"] <= max_per * u["attempted"]
                 and u["wrong_packets"] == 0 for u in users)
    root = Path(__file__).resolve().parents[4]
    try:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        revision = None
    return dict(schema_version=1, backend=phy, implementation=selected_engine,
                scope="shared-frame-PHY" if phy in ("ofdm", "ofdma") else "native-liquidDSP-frame-modem",
                topology=("single-user-ofdm" if phy == "ofdm" else "one-composite-transmitter-two-receivers") if phy in ("ofdm", "ofdma") else "sequential-single-user-baseline",
                upstream_revision=revision, python=platform.python_version(), numpy=np.__version__,
                seed=seed, frames=frames, payload_bytes_per_user=payload_bytes, config=asdict(config),
                channels=_serializable_channels(channels), users=users,
                simulated_airtime_seconds=duration,
                aggregate_payload_goodput_bps=sum(u["delivered"] for u in users) * payload_bytes * 8 / duration,
                worst_frame_papr_db=worst_papr if phy in ("ofdm", "ofdma") else None,
                acceptance=dict(max_packet_error_rate=max_per, passed=passed),
                limitations=["Static out-of-band allocations; uncoded QPSK with CRC32.",
                             "Burst IQ simulation, not the C++ MAC/ARQ/TAP/USRP path.",
                             "No sample-clock drift, Doppler, clipping, or RF impairments beyond the configured channel.",
                             "PER is empirical for this seed and frame count, not a confidence guarantee."])
