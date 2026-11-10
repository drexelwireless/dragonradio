# ---
# jupyter:
#   jupytext:
#     formats: ipynb,py:percent
#     text_representation:
#       extension: .py
#       format_name: percent
#   kernelspec:
#     display_name: DragonRadio (in-process PHY)
#     language: python
#     name: dragonradio
# ---

# %% [markdown]
# # Phase 0: native and liquid OFDM in the kernel (`phy` mode)
#
# This smoke notebook checks that a Jupyter kernel can call DragonRadio's PHYs
# in-process. It runs on the **`dragonradio` kernel**: the `dragonradio`
# executable started under the name `dragonradio-python`, which makes it a
# plain Python interpreter with the C++ module `dragonradiocpp` built in
# (`src/main.cc`). The kernel comes from `grid/build-notebook-layer.sh`.
#
# For both the **native OFDM** PHY and the **liquid OFDM** PHY it:
#
# 1. modulates packets with the production modulator,
# 2. adds white Gaussian noise in NumPy at several SNRs,
# 3. demodulates with the production demodulator, and
# 4. reports header and payload success and EVM, then plots a constellation
#    and a PSD.
#
# Everything here is **simulation**: an ideal AWGN channel, no radio, not real
# time.

# %% tags=["parameters"]
MODE = "phy"               # the only level this notebook supports
SNRS_DB = [-4, -2, 0, 2, 4, 6, 8, 10, 15, 20]
N_PACKETS = 100            # packets per PHY per SNR
PAYLOAD_BYTES = 500
SEED = 1
OUTDIR = "~/dragonradio-nb/results/phase0/phy"

# %%
import os
import time
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
from scipy import signal

import dragonradiocpp
import dragonradio.radio as radio
import dragonradio.liquid as liquid
from dragonradio.liquid import MCS, CRCScheme, FECScheme, ModulationScheme
from dragonradiocpp import notebook

assert MODE == "phy", f"this notebook supports MODE='phy' only, not {MODE!r}"
outdir = Path(os.path.expanduser(OUTDIR))
outdir.mkdir(parents=True, exist_ok=True)
tree = os.environ.get("DRAGONRADIO_TREE", "")
commit = Path(tree, "COMMIT").read_text().strip()[:8] if tree and Path(tree, "COMMIT").exists() else "unknown"
print("dragonradiocpp:", dragonradiocpp, "| tree:", tree, "| commit:", commit)
print("matplotlib backend:", matplotlib.get_backend())

# %% [markdown]
# ## The two PHYs
#
# Both use the radio's defaults (`dragonradio.config`): 48 subcarriers
# (`M`), a 6-sample cyclic prefix, default subcarrier allocation. The payload
# MCS is the same for both, the one `sim-fullstack.py` uses with liquid: CRC32,
# Reed-Solomon (255,223) outer code, no inner code, QPSK.
#
# * **Native OFDM** carries its own header section (v29 coded).
# * **Liquid OFDM** (`ofdmflexframe`) uses the radio's default header MCS:
#   CRC32, v29 rate 7/8, BPSK. Its symbols are tapered over 4 samples.

# %%
M, CP_LEN, TAPER_LEN = 48, 6, 4
payload_mcs = MCS(CRCScheme.crc32, FECScheme.rs8, FECScheme.none, ModulationScheme.qpsk)
header_mcs = MCS(CRCScheme.crc32, FECScheme.none, FECScheme.v29p78, ModulationScheme.bpsk)

phys = {
    "native": radio.NativeOFDM([(payload_mcs, radio.AutoGain())], M, CP_LEN, None, 4096, 4, True, "v29"),
    "liquid": liquid.OFDM(header_mcs, [(payload_mcs, radio.AutoGain())], True, True, M, CP_LEN, TAPER_LEN, None),
}
for name, phy in phys.items():
    print(f"{name:7s} {type(phy).__name__:10s} subcarriers: {phy.subcarriers}")

# %% [markdown]
# ## One EVM for both PHYs
#
# The EVM each PHY reports is defined differently, so the two numbers cannot be
# compared directly:
#
# * **Native** (`src/simulation/NativeFrame.hh`, `decode_payload`):
#   $10\log_{10}$ of the mean squared distance between each equalized
#   **payload data** symbol and the nearest constellation point (decision
#   directed; pilots and header excluded).
# * **Liquid** (`ofdmflexframesync.c`): $10\log_{10}$ of the mean squared
#   demodulator EVM over the **BPSK header** symbols only (decision directed).
#
# Both are decision directed, so at low SNR, where decisions are wrong, they
# read better than the truth.
#
# The **reference EVM** below is computed the same way for both, from the IQ,
# against the known transmitted samples. Cut the frame into OFDM symbols of
# $M + \text{CP}$ samples from its first sample, drop the CP, and take the
# FFT of the transmitted ($X$) and received ($Y$) symbol. On occupied
# subcarriers (where $|X|^2$ exceeds $10^{-3}$ of its maximum), fit one complex
# gain $h_s$ per OFDM symbol (this removes gain and common phase) and report
#
# $$\mathrm{EVM}_\mathrm{ref} = 10\log_{10}\frac{\sum |Y - h_s X|^2}{\sum |h_s X|^2}.$$
#
# It covers every occupied subcarrier of every symbol: preamble, pilots, header
# and payload. Because it is aided by the known data and the true timing, it
# measures the signal as received, not how well each receiver estimates the
# channel. In AWGN it should equal $-(\mathrm{SNR} + 10\log_{10}(M/\bar n))$,
# where $\bar n$ is the mean number of occupied subcarriers per symbol.

# %%
def reference_evm(tx, rx, M=M, cp=CP_LEN):
    """Data-aided EVM [dB] of rx against tx on the same symbol grid.

    Returns (evm_db, Z, X, mask): Z = Y/h_s (equalized received subcarriers),
    X the transmitted subcarriers, mask the occupied ones.
    """
    L = M + cp
    n = min(len(tx), len(rx)) // L
    X = np.fft.fft(tx[:n * L].reshape(n, L)[:, cp:], axis=1)
    Y = np.fft.fft(rx[:n * L].reshape(n, L)[:, cp:], axis=1)
    p = np.abs(X) ** 2
    mask = p > 1e-3 * p.max()
    h = (np.conj(X) * Y * mask).sum(axis=1) / np.maximum((p * mask).sum(axis=1), 1e-30)
    E = Y - h[:, None] * X
    evm = (np.abs(E) ** 2 * mask).sum() / (np.abs(h[:, None] * X) ** 2 * mask).sum()
    Z = np.where(np.abs(h[:, None]) > 0, Y / np.where(h[:, None] == 0, 1, h[:, None]), 0)
    return 10 * np.log10(evm), Z, X, mask


def awgn(buf, power, snr_db, rng):
    """buf plus complex white Gaussian noise at snr_db below power."""
    sigma = np.sqrt(power / 2 * 10 ** (-snr_db / 10))
    noise = sigma * (rng.standard_normal(len(buf)) + 1j * rng.standard_normal(len(buf)))
    return (buf + noise).astype(np.complex64)

# %% [markdown]
# ## Run
#
# Each packet is modulated alone and placed between 300 zero samples on each
# side. Noise is added over the whole buffer at the given SNR, defined as
# the frame's mean sample power over the noise power in the full 1 MHz band.
# The demodulator runs as node 2, and the packets are addressed to node 2.
#
# * **Header success**: the demodulator returned the frame, which means its
#   header decoded.
# * **Payload success**: the payload passed its CRC and matches the bytes sent.

# %%
GAP = 300
rng = np.random.default_rng(SEED)
rows = []
examples = {}
t0 = time.time()
for name, phy in phys.items():
    for snr in SNRS_DB:
        for k in range(N_PACKETS):
            payload = rng.integers(0, 256, PAYLOAD_BYTES, dtype=np.uint8).tobytes()
            seq = k % 65536
            x = notebook.modulate(phy, payload, seq=seq)
            buf = np.concatenate([np.zeros(GAP, np.complex64), x, np.zeros(GAP, np.complex64)])
            y = awgn(buf, np.mean(np.abs(x) ** 2), snr, rng)
            got = [d for d in notebook.demodulate(phy, y) if d["seq"] == seq]
            ref_db, Z, X, mask = reference_evm(x, y[GAP:GAP + len(x)])
            d = got[0] if got else None
            rows.append(dict(phy=name, snr_db=snr, header_ok=d is not None,
                             payload_ok=bool(d and d["payload_valid"] and d["data"] == payload),
                             phy_evm_db=d["evm"] if d else np.nan,
                             ref_evm_db=ref_db,
                             occupied=mask.sum(axis=1).mean(),
                             samples=len(x)))
            if k == 0:
                examples[(name, snr)] = (x, y[GAP:GAP + len(x)], Z, X, mask)
print(f"{len(rows)} packets in {time.time() - t0:.1f} s")
df = pd.DataFrame(rows)

# %%
summary = (df.groupby(["phy", "snr_db"])
             .agg(header=("header_ok", "mean"), payload=("payload_ok", "mean"),
                  phy_evm_db=("phy_evm_db", "mean"), ref_evm_db=("ref_evm_db", "mean"),
                  occupied=("occupied", "mean"), samples=("samples", "mean"))
             .reset_index())
summary["expected_ref_db"] = -(summary.snr_db + 10 * np.log10(M / summary.occupied))
summary.to_csv(outdir / "phy_summary.csv", index=False)
summary.round(2)

# %% [markdown]
# ## Look
#
# Delivery and EVM against SNR. The PHY-reported EVMs are the two
# incompatible definitions above. The reference EVM uses one definition for
# both.

# %%
fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 4.5))
for name, g in summary.groupby("phy"):
    a1.plot(g.snr_db, g.header, "o--", label=f"{name} header")
    a1.plot(g.snr_db, g.payload, "s-", label=f"{name} payload")
    a2.plot(g.snr_db, g.phy_evm_db, "o--", label=f"{name}: PHY-reported")
    a2.plot(g.snr_db, g.ref_evm_db, "s-", label=f"{name}: reference")
a1.set(xlabel="SNR per sample, full band [dB]", ylabel="success rate", title=f"Delivery, {PAYLOAD_BYTES}-byte payload, QPSK RS8")
a2.set(xlabel="SNR per sample, full band [dB]", ylabel="EVM [dB]", title="EVM: PHY-reported vs reference")
for a in (a1, a2):
    a.grid(True); a.legend(fontsize=8)
fig.tight_layout(); fig.savefig(outdir / "delivery_evm.png", dpi=120)

# %% [markdown]
# Constellations at 15 dB: the transmitted subcarrier values (left), and the
# received ones after the reference one-tap correction (right). The left
# column shows each PHY's frame structure: preamble, pilots, BPSK header
# (liquid) and QPSK payload.

# %%
snr_show = 15 if 15 in SNRS_DB else SNRS_DB[-1]
fig, axes = plt.subplots(2, 2, figsize=(10, 10))
for row, name in enumerate(phys):
    x, y, Z, X, mask = examples[(name, snr_show)]
    scale = np.sqrt(np.mean(np.abs(X[mask]) ** 2))
    axes[row, 0].plot(X[mask].real / scale, X[mask].imag / scale, ".", ms=3)
    axes[row, 1].plot(Z[mask].real / scale, Z[mask].imag / scale, ".", ms=2, alpha=0.4)
    axes[row, 0].set_title(f"{name}: transmitted subcarriers")
    axes[row, 1].set_title(f"{name}: received at {snr_show} dB")
    for a in axes[row]:
        a.set_aspect("equal"); a.grid(True); a.set_xlim(-2.5, 2.5); a.set_ylim(-2.5, 2.5)
fig.tight_layout(); fig.savefig(outdir / "constellation.png", dpi=120)

# %%
fig, ax = plt.subplots(figsize=(9, 4.5))
for name, phy in phys.items():
    frames = np.concatenate([notebook.modulate(phy, rng.integers(0, 256, PAYLOAD_BYTES, dtype=np.uint8).tobytes(), seq=i)
                             for i in range(20)])
    f, pxx = signal.welch(frames, fs=1e6, nperseg=512, return_onesided=False)
    f, pxx = np.fft.fftshift(f), np.fft.fftshift(pxx)
    ax.plot(f / 1e3, 10 * np.log10(pxx / pxx.max()), label=name)
ax.set(xlabel="frequency [kHz] (1 MHz sample rate)", ylabel="PSD [dB, normalized]", title="Transmitted PSD (20 frames, Welch, 512 bins)", ylim=(-40, 5))
ax.grid(True); ax.legend()
fig.tight_layout(); fig.savefig(outdir / "psd.png", dpi=120)

# %% [markdown]
# ## Checks
#
# These cells make the notebook a test.

# %% tags=["check"]
hi = summary[summary.snr_db == max(SNRS_DB)]
assert (hi.header == 1.0).all() and (hi.payload == 1.0).all(), hi
# The reference EVM follows the AWGN prediction within 1 dB wherever noise dominates.
mid = summary[summary.snr_db >= 0]
err = (mid.ref_evm_db - mid.expected_ref_db).abs().max()
assert err < 1.0, f"reference EVM off the AWGN prediction by {err:.2f} dB"
# Both PHYs lose packets at the lowest SNR (the sweep reaches the threshold).
lo = summary[summary.snr_db == min(SNRS_DB)]
assert (lo.payload < 1.0).all(), lo
print("checks passed; reference EVM max deviation from the AWGN prediction: %.2f dB" % err)
