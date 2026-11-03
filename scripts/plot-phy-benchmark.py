#!/usr/bin/env python3
"""Plot saved measured results (optional matplotlib dependency)."""
import argparse
import json
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path, default=Path("docs/images/phy-benchmark.png"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    os.environ.setdefault("MPLCONFIGDIR", str(root / "build/plot-cache"))
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    result = json.loads(args.report.read_text())
    fig, axes = plt.subplots(1, 4, figsize=(16, 4), layout="constrained")
    labels = ("Initial\nOFDMA", "Native\nOFDMA", "liquidDSP\nOFDM", "Native\nOFDM")
    values = []
    for table, name in (("ofdma_timing", "reference"), ("ofdma_timing", "native"), ("ofdm_timing", "liquid"), ("ofdm_timing", "native")):
        values.append(sum(result[table][name + suffix]["p50_us"] for suffix in ("_tx", "_rx")))
    colors = ("#9098a0", "#2879b9", "#d18128", "#278255")
    axes[0].bar(labels, values, color=colors)
    axes[0].set_yscale("log")
    axes[0].set_ylabel("TX + one RX median time (µs, log scale)")
    axes[0].set_title("Warm frame processing")
    for i, value in enumerate(values):
        axes[0].text(i, value * 1.05, f"{value:.1f}", ha="center", va="bottom", fontsize=9)
    axes[0].set_ylim(min(values) * 0.6, max(values) * 1.6)
    names = (("reference", "Initial OFDMA", colors[0], "--"),
             ("native_ofdma", "Native OFDMA", colors[1], "-"),
             ("liquid_ofdm", "liquidDSP OFDM", colors[2], "-"),
             ("native_ofdm", "Native OFDM", colors[3], "-"))
    for axis, profile in zip(axes[1:], result["quality_profiles"]):
        points = [p for p in result["quality"] if p["profile"] == profile]
        floor = 1 / (10 * points[0]["frames"])
        for key, label, color, style in names:
            axis.semilogy([p["snr_db"] for p in points], [max(floor, p["per"][key]) for p in points],
                          marker="o", linestyle=style, color=color, label=label, markersize=4)
        axis.set_title(profile.replace("-", " ").title())
        axis.set_xlabel("Time-domain channel SNR (dB)")
        axis.set_ylim(floor / 1.5, 1.5)
        axis.grid(True, which="both", alpha=0.2)
        axis.text(0.02, 0.02, "Zero errors shown at display floor", transform=axis.transAxes, fontsize=7)
    axes[1].set_ylabel("Empirical packet error rate")
    axes[2].legend(fontsize=8, loc="center right")
    fig.suptitle("Static OFDM/OFDMA: matched uncoded QPSK, 256-byte payloads, FFT 128 / CP 16", fontsize=12)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)
    plt.close(fig)
    print(args.output)


if __name__ == "__main__":
    main()
