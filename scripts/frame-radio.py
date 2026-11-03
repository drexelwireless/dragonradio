#!/usr/bin/env python3
"""Shared OFDM/OFDMA frame pipeline, with simulated or explicit UHD transport."""
import argparse
from dataclasses import asdict, replace
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python/dragonradio"))

import numpy as np
from dragonradio.simulation.ofdma import OFDMAConfig, Packet, ofdm_config
from dragonradio.simulation.frame import FrameEngine, ModulatedFrame
from dragonradio.simulation.runner import channel_profiles
from dragonradio.simulation.transport import SDRConfig, SimulatedTransport, UHDTransport


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("ofdm", "ofdma"), default="ofdma")
    parser.add_argument("--engine", choices=("auto", "native", "numpy"), default="auto")
    parser.add_argument("--transport", choices=("simulated", "uhd"), default="simulated")
    parser.add_argument("--profile", choices=("ideal", "awgn", "multipath"), default="multipath")
    parser.add_argument("--role", choices=("tx", "rx"))
    parser.add_argument("--sdr-config", type=Path)
    parser.add_argument("--user-id", type=int, default=1)
    parser.add_argument("--frames", type=int, default=100)
    parser.add_argument("--duration", type=float, default=5)
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--dry-run", action="store_true", help="Print the plan; never import UHD or open a radio")
    parser.add_argument("--output", type=Path, default=Path("simulation-results/frame-pipeline.json"))
    args = parser.parse_args(argv)
    if args.frames < 1 or args.frames > 10000 or args.seed < 0:
        parser.error("frames must be 1..10000 and seed nonnegative")
    raw_sdr = json.loads(args.sdr_config.read_text()) if args.sdr_config else None
    rate = raw_sdr.get("sample_rate", 1e6) if raw_sdr else 1e6
    cfg = ofdm_config(user_id=args.user_id, sample_rate=rate) if args.mode == "ofdm" else OFDMAConfig(sample_rate=rate)
    if args.dry_run:
        print(json.dumps(dict(mode=args.mode, transport=args.transport, role=args.role, config=asdict(cfg), sdr=raw_sdr, hardware_opened=False), indent=2))
        return 0
    if args.transport == "uhd" and (args.role is None or raw_sdr is None):
        parser.error("UHD requires an explicit --role and --sdr-config")
    engine = FrameEngine(cfg, args.engine)
    rng = np.random.default_rng(args.seed)
    expected = {}
    if args.transport == "simulated" or args.role == "tx":
        chunks, records = [], []
        offset = 0
        for sequence in range(args.frames):
            packets = [Packet(0, a.user_id, sequence * 3 + a.user_id, rng.bytes(256)) for a in cfg.allocations]
            expected.update({(p.destination, p.sequence): p for p in packets})
            frame = engine.build(packets)
            chunks.extend((frame.samples, np.zeros(64, dtype=np.complex64)))
            records.extend(replace(record, sample_offset=offset) for record in frame.records)
            offset += len(frame.samples) + 64
        burst = ModulatedFrame(np.concatenate(chunks), tuple(records), cfg.sample_rate, engine.implementation)
    if args.transport == "simulated":
        outcomes = {}
        for index, allocation in enumerate(cfg.allocations):
            samples = SimulatedTransport(cfg, channel_profiles(args.profile)[index], args.seed + index).exchange(burst)
            decoder = engine.stream_receiver(allocation.user_id)
            received, offset, chunk_index = [], 0, 0
            while offset < len(samples):
                size = (97, 511, 2048, 13, 1000)[chunk_index % 5]
                received.extend(decoder.feed(samples[offset:offset + size]))
                offset += size
                chunk_index += 1
            received.extend(decoder.feed([], final=True))
            unique = {r.packet.sequence: r.packet for r in received if r.ok}
            correct = sum(p == expected.get((allocation.user_id, sequence)) for sequence, p in unique.items())
            outcomes[allocation.user_id] = dict(delivered=correct, expected=args.frames,
                                                failed_frames=sum(not r.ok for r in received),
                                                remaining_buffer_samples=len(decoder.buffer))
        passed = all(u["delivered"] == args.frames for u in outcomes.values())
        report = dict(mode=args.mode, transport="simulated", implementation=engine.implementation,
                      frames=args.frames, users=outcomes, passed=passed, hardware_opened=False)
    else:
        sdr = SDRConfig(**raw_sdr)
        if sdr.sample_rate != cfg.sample_rate:
            parser.error("SDR and PHY rates must match")
        transport = UHDTransport(sdr, args.role)
        if args.role == "tx":
            report = dict(mode=args.mode, transport="uhd", role="tx", result=transport.transmit(burst),
                          packets=[dict(source=r.packet.source, destination=r.packet.destination, sequence=r.packet.sequence,
                                        sample_offset=r.sample_offset, sample_count=r.sample_count) for r in burst.records])
        else:
            results = transport.receive(engine.stream_receiver(args.user_id), args.duration)
            report = dict(mode=args.mode, transport="uhd", role="rx", user_id=args.user_id,
                          decoded=[dict(source=r.packet.source, destination=r.packet.destination, sequence=r.packet.sequence,
                                        payload_hex=r.packet.payload.hex(), sample_offset=r.start_sample, evm_db=r.evm_db)
                                   for r in results if r.ok], failed_frames=sum(not r.ok for r in results))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report.get("passed", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
