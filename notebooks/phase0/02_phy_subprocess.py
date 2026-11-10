# ---
# jupyter:
#   jupytext:
#     formats: ipynb,py:percent
#     text_representation:
#       extension: .py
#       format_name: percent
#   kernelspec:
#     display_name: Python 3 (notebook layer)
#     language: python
#     name: drnb-python3
# ---

# %% [markdown]
# # Phase 0: `phy` mode through a subprocess (fallback)
#
# The same packet loopback as `01_phy_inprocess`, but the kernel is plain
# Python with no `dragonradiocpp`. The PHYs run in a subprocess,
# `scripts/phy-npz.py`, started with the notebook build's `dragonradio-python`.
# Arrays travel through `.npz` files, and the AWGN channel stays in the
# notebook.
#
# This is the fallback for a kernel that cannot host DragonRadio, for example
# a stock Colab runtime with the executable unpacked next to it. It is
# **simulation**: an ideal AWGN channel, not real time.

# %% tags=["parameters"]
MODE = "phy"
SNRS_DB = [0, 4, 8, 12]
N_PACKETS = 50
PAYLOAD_BYTES = 500
SEED = 2
NBVENV = "/tmp/drnb/nbvenv"
OUTDIR = "~/dragonradio-nb/results/phase0/phy-subprocess"

# %%
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
import pandas as pd

assert MODE == "phy"
try:
    import dragonradiocpp.radio  # noqa: F401
    raise AssertionError("this kernel can import dragonradiocpp; the fallback is meant for one that cannot")
except ModuleNotFoundError as e:
    print("as expected, no dragonradiocpp in this kernel:", e)

tree = Path(os.environ.get("DRAGONRADIO_TREE", "/tmp/drnb/dragonradio"))
script = tree / "scripts" / "phy-npz.py"
exe = Path(NBVENV) / "bin" / "dragonradio-python"
outdir = Path(os.path.expanduser(OUTDIR))
outdir.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix="drnb-phy-"))
print(sys.executable, "|", exe, "|", script)

# %%
def phy_npz(op, phy, arrays, timeout=120):
    """Run scripts/phy-npz.py OP --phy PHY on arrays; return its output arrays and wall time."""
    inp, out = work / f"{op}-{phy}-in.npz", work / f"{op}-{phy}-out.npz"
    np.savez(inp, **arrays)
    t0 = time.time()
    proc = subprocess.run([str(exe), str(script), op, "--phy", phy, str(inp), str(out)],
                          capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"phy-npz.py {op} {phy} failed ({proc.returncode}):\n{proc.stderr[-2000:]}")
    with np.load(out) as z:
        return {k: z[k] for k in z.files}, time.time() - t0

# %%
GAP = 300
rng = np.random.default_rng(SEED)
rows, calls = [], []
for phy in ["native", "liquid"]:
    payloads = rng.integers(0, 256, (N_PACKETS, PAYLOAD_BYTES), dtype=np.uint8)
    mod, dt = phy_npz("modulate", phy, dict(payloads=payloads, seqs=np.arange(N_PACKETS)))
    calls.append((phy, "modulate", N_PACKETS, dt))
    frames = [mod["iq"][mod["offsets"][k]:mod["offsets"][k + 1]] for k in range(N_PACKETS)]
    for snr in SNRS_DB:
        bufs = []
        for x in frames:
            buf = np.concatenate([np.zeros(GAP, np.complex64), x, np.zeros(GAP, np.complex64)])
            sigma = np.sqrt(np.mean(np.abs(x) ** 2) / 2 * 10 ** (-snr / 10))
            bufs.append((buf + sigma * (rng.standard_normal(len(buf)) + 1j * rng.standard_normal(len(buf)))).astype(np.complex64))
        offsets = np.concatenate([[0], np.cumsum([len(b) for b in bufs])])
        dem, dt = phy_npz("demodulate", phy, dict(iq=np.concatenate(bufs), offsets=offsets))
        calls.append((phy, f"demodulate {snr} dB", N_PACKETS, dt))
        ok = np.zeros(N_PACKETS, bool)
        for i in np.flatnonzero(dem["payload_valid"]):
            k = int(dem["buffer"][i])
            ok[k] |= bool(dem["seq"][i] == k and dem["data_len"][i] == PAYLOAD_BYTES
                          and np.array_equal(dem["data"][i, :PAYLOAD_BYTES], payloads[k]))
        rows.append(dict(phy=phy, snr_db=snr, header=len(set(dem["buffer"].tolist())) / N_PACKETS,
                         payload=ok.mean(), phy_evm_db=float(np.mean(dem["evm"])) if len(dem["evm"]) else np.nan))
summary = pd.DataFrame(rows)
summary.to_csv(outdir / "phy_subprocess_summary.csv", index=False)
summary.round(2)

# %% [markdown]
# Subprocess cost: each call starts the executable, imports `dragonradio`
# and builds the PHY.

# %%
calls = pd.DataFrame(calls, columns=["phy", "call", "packets", "wall_s"])
calls.to_csv(outdir / "phy_subprocess_calls.csv", index=False)
calls.round(3)

# %% tags=["check"]
hi = summary[summary.snr_db == max(SNRS_DB)]
assert (hi.payload == 1.0).all(), hi
assert calls.wall_s.max() < 30, calls
print("checks passed")
