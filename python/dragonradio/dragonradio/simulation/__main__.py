import argparse
import json
from pathlib import Path
import sys

from .ofdma import OFDMAConfig, ofdm_config
from .runner import channel_profiles, run_experiment


def main(argv=None):
    parser = argparse.ArgumentParser(description="Hardware-free DragonRadio PHY simulation and static two-user OFDMA prototype")
    parser.add_argument("--phy", choices=("ofdm", "ofdma", "liquid-ofdm"), default="ofdma")
    parser.add_argument("--engine", choices=("auto", "numpy", "native"), default="auto")
    parser.add_argument("--profile", choices=("ideal", "awgn", "multipath"), default="multipath")
    parser.add_argument("--frames", type=int, default=100)
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--payload-bytes", type=int, default=256)
    parser.add_argument("--max-payload", type=int, default=256)
    parser.add_argument("--snr-db", type=float)
    parser.add_argument("--snr-sweep", help="Comma-separated SNR points; a negative first value needs =, e.g. --snr-sweep=-5,0,10,20")
    parser.add_argument("--cfo-subcarriers", nargs=2, type=float, metavar=("USER1", "USER2"))
    parser.add_argument("--delay-samples", nargs=2, type=int, metavar=("USER1", "USER2"))
    parser.add_argument("--nfft", type=int, default=128)
    parser.add_argument("--cp-len", type=int, default=16)
    parser.add_argument("--sample-rate", type=float, default=1e6)
    parser.add_argument("--max-per", type=float, default=0.01, help="Maximum per-user PER for a successful exit (default 0.01)")
    parser.add_argument("--output", type=Path, help="Write full JSON results")
    parser.add_argument("--export-iq", type=Path, help="Write first OFDMA frame's TX/RX samples and expected payloads as NPZ")
    args = parser.parse_args(argv)
    try:
        if args.seed < 0:
            raise ValueError("seed must be nonnegative")
        config = (ofdm_config if args.phy == "ofdm" else OFDMAConfig)(nfft=args.nfft, cp_len=args.cp_len, sample_rate=args.sample_rate, max_payload=args.max_payload)
        if args.snr_sweep and args.snr_db is not None:
            raise ValueError("choose --snr-db or --snr-sweep")
        snrs = [float(s) for s in args.snr_sweep.split(",")] if args.snr_sweep else [args.snr_db]
        if args.snr_sweep and args.export_iq:
            raise ValueError("export IQ from a single SNR run")
        runs = []
        for snr in snrs:
            channels = channel_profiles(args.profile, snr, args.cfo_subcarriers, args.delay_samples)
            run = run_experiment(config, args.frames, args.seed, channels, args.payload_bytes,
                                 args.max_per, args.phy, args.export_iq, args.engine)
            runs.append(run)
            print("{} | {} | SNR {} | {} frames | {}".format(args.phy, args.profile, snr, args.frames,
                  "PASS" if run["acceptance"]["passed"] else "FAIL"))
            for user in run["users"]:
                print("  user {}: {}/{} verified, PER={:.4f}, mean EVM={} dB".format(
                    user["user_id"], user["delivered"], user["attempted"], user["packet_error_rate"], user["mean_evm_db"]))
        report = runs[0] if len(runs) == 1 else dict(schema_version=1, runs=runs)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        return 0 if all(r["acceptance"]["passed"] for r in runs) else 1
    except (ValueError, RuntimeError, OSError) as exc:
        print("simulation: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
