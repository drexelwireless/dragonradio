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
# # Phase 0: drgui in a notebook (ipympl)
#
# This notebook loads the HDF5 logs of a short simulated `clean-arq` run, once
# with the native PHY and once with liquid, using `dragonradio.tools.logging`.
# It then opens drgui's receive, send and snapshot views unchanged under
# `%matplotlib widget` (ipympl), and draws the reusable plot classes directly.
#
# The logs come from `grid/run-sim-fullstack.sh ... --scenario clean-arq
# --duration 6 -- --log-iq --log-tx-records --log-snapshots
# --snapshot-frequency 1.0 --snapshot-duration 0.1`. They are **simulation**:
# simulated device and air, nothing over the air.
#
# The views are driven the way a user would drive them: plot, then the
# Next button, the slider and "Link to TX". Every step records what works and
# what breaks in a table at the end, so this notebook is a test as well as a
# report.

# %% tags=["parameters"]
MODE = "load"
RUNS = {
    "native": "/tmp/drnb/sim/drgui-native/clean-arq/logs",
    "liquid": "/tmp/drnb/sim/drgui-liquid/clean-arq/logs",
}
RX_NODE, TX_NODE = 2, 1
OUTDIR = "~/dragonradio-nb/results/phase0/drgui"

# %%
# %matplotlib widget

# %%
import os
import time
import traceback
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

assert MODE == "load"
outdir = Path(os.path.expanduser(OUTDIR))
outdir.mkdir(parents=True, exist_ok=True)
print("backend:", matplotlib.get_backend())

results = []


def step(run, what, fn):
    """Run one step; record OK or the exception, and keep going."""
    t0 = time.time()
    try:
        out = fn()
        results.append(dict(run=run, step=what, ok=True, seconds=round(time.time() - t0, 2), detail=""))
        return out
    except Exception as e:
        tb = traceback.extract_tb(e.__traceback__)[-1]
        detail = f"{type(e).__name__}: {e} ({Path(tb.filename).name}:{tb.lineno})"
        results.append(dict(run=run, step=what, ok=False, seconds=round(time.time() - t0, 2), detail=detail))
        print(f"[{run}] {what}: {detail}")
        return None

# %% [markdown]
# ## Load the logs
#
# `LogCollection` reads one `radio.h5` per node. Importing
# `dragonradio.tools.logging` needs three compiled pieces: `dragonradio.liquid`
# for the CRC, FEC and modulation enums, `dragonradio.signal.decompressIQData`
# for slot and snapshot IQ, and `dragonradiocpp_tools_mgen`. This kernel has
# all three.

# %%
from dragonradio.tools.logging import LogCollection
from dragonradio.tools.plot.radio.drgui import LogViewer
from dragonradio.tools.plot.radio.plot import (ConstellationPlot, EventPlot, PSDPlot, SpecgramPlot, TrafficPlot,
                                               pprReceivedPacket, pprSentPacket)
from dragonradio.signal import decompressIQData

logs = {}
for run, d in RUNS.items():
    def load(d=d):
        lc = LogCollection()
        lc.load(sorted(str(p) for p in Path(d).glob("node-*/radio.h5")))
        return lc
    lc = step(run, "load LogCollection", load)
    if lc is None:
        continue
    logs[run] = lc
    sizes = {n: dict(recv=len(lc[n].recv), send=len(lc[n].send), slots=len(lc[n].slots),
                     snapshots=len(lc[n].snapshots), events=len(lc[n].events))
             for n in sorted(lc.nodes)}
    print(run, sizes)

# %% [markdown]
# ## drgui's views, unchanged
#
# `LogViewer.rxView`, `txView` and `snapshotView` build the same figures as
# the `drgui` command, including matplotlib's Slider and Button widgets.
# Under ipympl each figure is an interactive canvas widget, and `savefig`
# still writes a PNG.

# %%
# drgui sizes nothing itself; on a desktop the window manager gives it room.
plt.rcParams["figure.figsize"] = (13, 8)
views = {}
for run, lc in logs.items():
    viewer = LogViewer(lc)
    rx = step(run, "rxView(node 2)", lambda: viewer.rxView(RX_NODE))
    if rx is not None:
        step(run, "rx.plot(0)", lambda: rx.plot(0))
        step(run, "rx Next button", lambda: rx.next_packet(None))
        step(run, "rx slider -> packet 5", lambda: rx.spos.set_val(5))
        def slider_moved(rx=rx):
            assert rx.pktidx == 5, f"pktidx {rx.pktidx}"
        step(run, "rx index after slider == 5", slider_moved)
        step(run, "rx Link to TX", lambda: rx.link_to_tx(None))
        step(run, "rx savefig", lambda: rx.fig.savefig(outdir / f"{run}_rxview.png", dpi=100))
    tx = step(run, "txView(node 1)", lambda: viewer.txView(TX_NODE))
    if tx is not None:
        step(run, "tx.plot(0)", lambda: tx.plot(0))
        step(run, "tx Link to RX", lambda: tx.link_to_rx(None))
        step(run, "tx savefig", lambda: tx.fig.savefig(outdir / f"{run}_txview.png", dpi=100))
    snap = step(run, "snapshotView(node 2)", lambda: viewer.snapshotView(RX_NODE))
    if snap is not None:
        step(run, "snapshot.plot(0)", lambda: snap.plot(0))
        step(run, "snapshot Next button", lambda: snap.next_snapshot(None))
        step(run, "snapshot savefig", lambda: snap.fig.savefig(outdir / f"{run}_snapshotview.png", dpi=100))
    views[run] = (rx, tx, snap)

# %% [markdown]
# ## The reusable plot classes
#
# Each class draws onto axes we hand it. The constellation uses the
# received symbols the PHY logged with the packet (`--log-iq` turns on
# `log_recv_symbols`). Only liquid fills `RadioPacket.symbols`, so for native
# the constellation is drawn from the transmitted IQ instead.

# %%
for run, lc in logs.items():
    fig, axes = plt.subplots(1, 3, figsize=(15, 4))
    recv = lc[RX_NODE].recv
    recv = recv[recv.header_valid == True]
    pkt = recv.iloc[min(5, len(recv) - 1)]
    def constellation():
        syms = pkt.symbols if isinstance(pkt.symbols, np.ndarray) and len(pkt.symbols) else None
        if syms is None:
            raise ValueError(f"no received symbols logged for {run} (symbols={type(pkt.symbols).__name__}, len {len(pkt.symbols) if hasattr(pkt.symbols, '__len__') else '-'})")
        ConstellationPlot(fig, axes[0]).plot(syms, title=f"{run}: received symbols")
    step(run, "ConstellationPlot(received symbols)", constellation)
    if not results[-1]["ok"]:
        sent = lc[TX_NODE].send.iloc[0].iq_data
        step(run, "ConstellationPlot(sent IQ, fallback)", lambda: ConstellationPlot(fig, axes[0]).plot(sent, title=f"{run}: sent IQ (no rx symbols)"))
    slots = lc.findPacketSlots(pkt, RX_NODE)
    sig = slots[pkt.start_samples:pkt.end_samples] if slots is not None else None
    step(run, "PSDPlot(received packet)", lambda: PSDPlot(fig, axes[1], nfft=256).plot(slots.fs, sig))
    step(run, "SpecgramPlot(received slots)", lambda: SpecgramPlot(fig, axes[2], nfft=256).plot(slots.fs, slots.iq_data, slots.t_start))
    fig.tight_layout()
    step(run, "plot classes savefig", lambda: fig.savefig(outdir / f"{run}_plotclasses.png", dpi=100))

    def events():
        ev = EventPlot(lc)
        for n in sorted(lc.nodes, reverse=True):
            ev.addEventCategory(n, "SEND", ppr=pprSentPacket)
            ev.addEventCategory(n, "RECV", ppr=pprReceivedPacket)
            ev.addEventCategory(n, "TXRECORD", ppr=lambda r: str(r))
        ev.plot()
        ev.fig.savefig(outdir / f"{run}_events.png", dpi=100)
        return ev
    step(run, "EventPlot(SEND, RECV, TXRECORD)", events)

    def traffic():
        tp = TrafficPlot(plt.figure(figsize=(12, 6)), lc, TX_NODE, RX_NODE)
        tp.plot()
        tp.fig.savefig(outdir / f"{run}_traffic.png", dpi=100)
        return tp
    step(run, "TrafficPlot(1 -> 2)", traffic)

# %% [markdown]
# ## What worked and what broke

# %%
report = pd.DataFrame(results)
report.to_csv(outdir / "drgui_steps.csv", index=False)
pd.set_option("display.max_colwidth", 200)
report

# %% [markdown]
# Known breakage (Phase 0): `TrafficPlot` calls `CheckButtons.rectangles`
# (`dragonradio/tools/plot/util.py`, `addCheckboxWidget`). Matplotlib removed
# that attribute (deprecated in 3.7), and the image has 3.10. This is not
# specific to ipympl: the `plot-traffic` command fails the same way. The
# check below expects this failure, so it flags the fix when it lands.

# %% tags=["check"]
must = ["load LogCollection", "rxView(node 2)", "rx.plot(0)", "rx Next button", "rx slider -> packet 5",
        "rx Link to TX", "txView(node 1)", "tx.plot(0)", "tx Link to RX", "snapshotView(node 2)",
        "snapshot.plot(0)", "PSDPlot(received packet)", "SpecgramPlot(received slots)",
        "EventPlot(SEND, RECV, TXRECORD)"]
known_broken = {"TrafficPlot(1 -> 2)": "has no attribute 'rectangles'"}
bad = report[report.step.isin(must) & ~report.ok]
assert bad.empty, bad
for what, msg in known_broken.items():
    rows = report[report.step == what]
    assert (~rows.ok & rows.detail.str.contains(msg, regex=False)).all(), f"{what} changed: update known_broken\n{rows}"
print("checks passed:", int(report.ok.sum()), "of", len(report), "steps OK; known broken:", list(known_broken))
