# ---
# jupyter:
#   jupytext:
#     formats: py:percent
#     text_representation:
#       extension: .py
#       format_name: percent
#   kernelspec:
#     display_name: DragonRadio (in-process PHY)
#     language: python
#     name: dragonradio
# ---

# %% [markdown]
# # DragonRadio in a notebook: a first run and the drgui views
#
# This notebook runs the real DragonRadio stack (PHY, synthesizer,
# channelizer, ARQ, queues and TDMA MAC) between two simulated nodes. Only the
# radio device and the air between the nodes are simulated, so nothing is
# transmitted. It then opens the same views as the `drgui` tool on the run's
# logs:
#
# * **Receive view:** a spectrogram of the received slots with each packet
#   bracketed, and below it the selected packet's constellation, waveform,
#   power spectral density and PAPR. Step through packets with the slider and
#   the Next/Prev buttons. *Link to TX* opens the packet's transmission.
# * **Send view:** the same panels for transmitted packets.
# * **Snapshot view:** short raw IQ captures of the whole channel.
# * **Event plot:** sends and receives at both nodes on one time line.
#
# **How to use it.** Set the parameters in the next cell, then
# *Run → Run All Cells*. A run takes about 30 seconds. Change one parameter
# at a time and rerun.
#
# **Predict first.** Before each rerun, write down what you expect to change in
# the spectrogram, the constellation and the delivery table.

# %% tags=["parameters"]
SCENARIO = "clean-arq"   # clean-arq, erasure-arq, erasure-noarq, impaired, jitter, saturation, asymmetric, ack-loss
PHY = "liquid"           # "liquid" (OFDM, received constellation logged) or "native" (no received symbols logged)
NOISE_DB = -50.0         # noise power per sample at each receiver, dB. Raise it (e.g. -25) to lower the SNR
DURATION = 6.0           # seconds of traffic
RX_NODE, TX_NODE = 2, 1  # node 1 sends to node 2 in every scenario above

# %%
# NumPy warns, harmlessly, about float subnormals when the radio's Python starts.
import warnings
warnings.filterwarnings("ignore", message="The value of the smallest subnormal")

# %matplotlib widget

# %%
import json
import os
import subprocess
import time
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
from IPython.display import display

TREE = os.environ.get("DRAGONRADIO_TREE", "/opt/drnb/dragonradio")
RUNROOT = Path(os.environ.get("DRNB_RUNS", f"/tmp/drnb-{os.environ.get('USER', 'user')}/runs"))
plt.rcParams["figure.figsize"] = (8, 6)   # fits the notebook column; drgui's views use this size
# ipympl sends only changed pixels after the first image; on a HiDPI screen the
# first resize can leave a figure partly drawn. Any non-opaque pixel makes
# matplotlib send whole images, so give figures a 99%-opaque background.
plt.rcParams["figure.facecolor"] = (1, 1, 1, 0.99)
plt.ioff()   # each figure is displayed once, empty, and then drawn (the order ipympl handles reliably)
pd.set_option("display.max_colwidth", 120)

# %% [markdown]
# ## Run the simulation
#
# Both nodes log every packet with its IQ samples (`--log-iq`), every
# transmission record, and one 0.1 s snapshot of the channel per second. The run
# uses the CPU core your notebook server was given.

# %%
phy_args = {"liquid": ["--phy", "ofdm", "-c", "rs8"], "native": []}[PHY]
log_args = ["--log-iq", "--log-tx-records", "--log-snapshots",
            "--snapshot-frequency", "1.0", "--snapshot-duration", "0.1"]
tag = f"{SCENARIO}-{PHY}-{NOISE_DB:g}dB-{time.strftime('%H%M%S')}"
rundir = RUNROOT / tag
rundir.parent.mkdir(parents=True, exist_ok=True)
cmd = ["bash", f"{TREE}/grid/run-sim-fullstack.sh", TREE, str(rundir), "--scenario", SCENARIO,
       "--noise-db", str(NOISE_DB), "--duration", str(DURATION), "--", *phy_args, *log_args]
print("running:", " ".join(cmd[2:]))
t0 = time.time()
proc = subprocess.run(cmd, capture_output=True, text=True)
print(f"finished in {time.time() - t0:.0f} s, exit {proc.returncode}")
print("\n".join(proc.stdout.strip().splitlines()[-12:]))
if proc.returncode not in (0, 1):   # 1 = a check failed; the logs are still there
    print(proc.stderr[-3000:])

# %% [markdown]
# ## Results: the scenario's checks and each traffic flow
#
# *Delivery* counts application packets that arrived. *Frame delivery* counts
# PHY frames, including retransmissions. With ARQ on, lost frames are resent,
# so delivery can stay at 100% while frame delivery drops.

# %%
summary = json.loads((rundir / SCENARIO / "summary.json").read_text())
print("PASSED" if summary["passed"] else "FAILED", "-", summary.get("doc", SCENARIO))
display(pd.DataFrame(summary["checks"]))
flows = pd.DataFrame(summary.get("flows", []))
cols = [c for c in ["flow", "sent", "delivered", "delivery", "app_duplicates", "latency_p50_ms", "latency_p95_ms"] if c in flows]
display(flows[cols])
links = pd.DataFrame(summary.get("links_accounting", {})).T
display(links[[c for c in ["frames_sent", "frames_received", "frame_delivery", "data_attempts", "retransmissions"] if c in links]])

# %% [markdown]
# ## Load the logs
#
# Each node wrote one HDF5 file. `LogCollection` reads them the same way
# `drgui` does.

# %%
from dragonradio.tools.logging import LogCollection
from dragonradio.tools.plot.radio.drgui import LogViewer
from dragonradio.tools.plot.radio.plot import EventPlot, pprReceivedPacket, pprSentPacket

logs = LogCollection()
logs.load(sorted(str(p) for p in (rundir / SCENARIO / "logs").glob("node-*/radio.h5")))
viewer = LogViewer(logs)
pd.DataFrame({n: dict(received=len(logs[n].recv), sent=len(logs[n].send), slots=len(logs[n].slots),
                      snapshots=len(logs[n].snapshots)) for n in sorted(logs.nodes)})

# %% [markdown]
# ## Receive view (node 2)
#
# Top: spectrogram of the received slots; the selected packet is bracketed.
# Bottom, left to right: constellation, waveform, PSD and PAPR of that packet.
# Use the slider or Next/Prev to move between packets. With `PHY = "native"`
# the constellation stays empty, because the native PHY does not log received
# symbols yet.

# %%
rx = viewer.rxView(RX_NODE, show=False)
display(rx.fig.canvas)
rx.plot(0)

# %% [markdown]
# ## Send view (node 1)

# %%
tx = viewer.txView(TX_NODE, show=False)
display(tx.fig.canvas)
tx.plot(0)

# %% [markdown]
# ## Snapshot view (node 2)
#
# Raw IQ of the whole channel, captured once a second for 0.1 s. The view
# opens at a snapshot from the middle of the run, while traffic is flowing (the
# first few are from the warm-up, before any packets). Look for both nodes' bursts in their
# TDMA slots, and step back with Previous to see the empty channel.

# %%
start = len(logs[RX_NODE].snapshots) // 2   # mid-run: warm-up and drain are 4 s each
snap = viewer.snapshotView(RX_NODE, show=False)
display(snap.fig.canvas)
snap.plot(start)

# %% [markdown]
# ## Event plot
#
# Every send and receive at both nodes on one time line. Zoom in with the
# toolbar to see individual slots, retransmissions and ACKs.

# %%
ev = EventPlot(logs, fig=plt.figure(figsize=(8, 3.5)))
display(ev.fig.canvas)
for n in sorted(logs.nodes, reverse=True):
    ev.addEventCategory(n, "SEND", ppr=pprSentPacket)
    ev.addEventCategory(n, "RECV", ppr=pprReceivedPacket)
ev.plot()

# %% [markdown]
# ## Try next
#
# 1. **Noise.** Rerun with `NOISE_DB = -30`, then `-20`. What happens to the
#    constellation, the frame delivery and the number of retransmissions?
# 2. **ARQ.** Compare `erasure-noarq` with `erasure-arq` (20% of bursts erased
#    on the 1→2 link). Which table shows the erasures, and which hides them?
# 3. **PHY.** Rerun `clean-arq` with `PHY = "native"`. Compare the spectrogram
#    and the latency with liquid.
#
# Notes. Logged IQ is stored with 14 bits, like the X310's ADC, so samples at
# or above full scale are clipped in the logs (the radio itself demodulated the
# unclipped signal). drgui's traffic plot does not yet work with this
# matplotlib version.
#
# **If a control does not respond:** click once on the figure first; while the
# figure has focus, keys are matplotlib shortcuts (`p` pan, `o` zoom), so
# press the same key again to leave that mode. You can also move from code:
# `rx.plot(5)` shows received packet 5.
