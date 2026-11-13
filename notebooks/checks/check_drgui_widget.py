"""Headless test of DrGUI's controls on a run's logs (inline renderer, Agg).

Usage (in the dragonradio-nb image):
    dragonradio-python notebooks/checks/check_drgui_widget.py RUNDIR OUTDIR

RUNDIR is a run directory written by drgui_start (or grid/run-sim-fullstack.sh
with --log-iq --log-tx-records --log-snapshots); figures go to OUTDIR.
"""
import sys, warnings
from pathlib import Path
warnings.filterwarnings("ignore", message="The value of the smallest subnormal")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from dragonradio.tools.logging import LogCollection
import dragonradio.tools.plot.radio.notebook as nbmod
from dragonradio.tools.plot.radio.notebook import DrGUI

rundir, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
print("module:", nbmod.__file__)
logs = LogCollection()
logs.load(sorted(str(p) for p in rundir.glob("**/logs/node-*/radio.h5")))

fails = []
def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails.append(msg)

def save(gui, name):
    gui.current.fig.savefig(out / f"{name}.png", dpi=80)

def status(gui):
    return gui.w_status.value

gui = DrGUI(logs, view="receive", node=2, renderer="inline")
gui.show()
n_rx = gui.w_index.max + 1
check(gui.renderer == "inline", "renderer inline under Agg")
check(n_rx == len(logs[2].recv[logs[2].recv.header_valid == True]), f"receive index covers {n_rx} valid-header packets")
check("of" in gui.w_count.value and status(gui) == "", f"first draw: '{gui.w_count.value}' status '{status(gui)}'")
check("seq" in gui.w_info.value, "info table shows seq")
save(gui, "rx0")
title0 = gui.current.fig._suptitle.get_text()

gui.w_next.click()
check(gui.index == 1, "Next -> index 1")
check(gui.current.fig._suptitle.get_text() != title0, "figure title changed after Next")
gui.w_prev.click()
check(gui.index == 0, "Previous -> index 0")
gui.index = n_rx // 2
check(gui.current.pktidx == n_rx // 2, f"slider -> drew packet {n_rx // 2}")
save(gui, "rx_mid")

# every received packet draws, with IQ found
bad = []
for i in range(n_rx):
    gui.index = i
    if status(gui):
        bad.append((i, status(gui)))
check(not bad, f"all {n_rx} received packets drew without warnings ({len(bad)} bad: {bad[:3]})")

# seq jump
seq = int(gui._items().seq.iloc[10])
gui.w_seq.value = seq
gui.w_goseq.click()
check(int(gui.current.pkt.seq) == seq, f"Go to seq {seq}")

# overlays, nfft, sigslop, zoom
for k, w in gui.w_overlays.items():
    w.value = not w.value
check(all(getattr(gui.current, k) == w.value for k, w in gui.w_overlays.items()), "overlay checkboxes reach the view")
for k, w in gui.w_overlays.items():
    w.value = not w.value
gui.w_sigslop.value = 100
check(gui.current.sigslop == 100 and status(gui) == "", "extra samples 100")
gui.w_nfft.value = 512
check(gui.current.specgram.nfft == 512, "FFT 512 uses a 512-point view")
x0, x1 = gui.current.specgram.ax.get_xlim()
gui.w_zoom.value = (0.25, 0.5)
z0, z1 = gui.current.specgram.ax.get_xlim()
check(abs((z1 - z0) - 0.25 * (x1 - x0)) < 1e-9 * max(1, x1 - x0) + 1e-12, f"time zoom to 25% ({x0:.4g},{x1:.4g})->({z0:.4g},{z1:.4g})")
save(gui, "rx_zoom")
gui.w_zoom.value = (0.0, 1.0)
gui.w_nfft.value = 256

# filters
gui.w_filter.value = "all"
check(gui.w_index.max + 1 == len(logs[2].recv), f"filter all: {gui.w_index.max + 1}")
gui.w_filter.value = "bad-payload"
print("     bad-payload packets:", gui.w_index.max + 1 if not gui.w_index.disabled else 0, status(gui))
gui.w_filter.value = "valid"

# link to TX
gui.index = 5
rx_seq = int(gui.current.pkt.seq)
gui.w_link.click()
check(gui.view == "send" and gui.node == 1, f"Link to TX -> send view node {gui.node}")
check(int(gui.current.pkt.seq) == rx_seq, f"linked TX packet has seq {rx_seq}")
save(gui, "tx_linked")
check(gui.w_link.description == "Link to RX", "button now Link to RX")
gui.w_link.click()
check(gui.view == "receive" and gui.node == 2 and int(gui.current.pkt.seq) == rx_seq, "Link to RX returns to the packet")

# send view stepping
gui.view = "send"
check(gui.view == "send" and gui.index == 0 and status(gui) == "", f"send view, node {gui.node}")
gui.w_next.click(); gui.w_next.click()
check(gui.current.pktidx == 2, "send Next x2")

# snapshot view
gui.view = "snapshot"
ns = gui.w_index.max + 1
check(ns == len(logs[gui.node].snapshots) and gui.index == ns // 2, f"snapshot view opens mid-run ({gui.index} of {ns})")
save(gui, "snap_mid")
gui.w_next.click()
check(gui.current.snapshotidx == ns // 2 + 1, "snapshot Next")
gui.w_zoom.value = (0.0, 0.2)
save(gui, "snap_zoom")
check("Snapshot" in gui.w_info.value, "snapshot info table")

# node switch
gui.view = "receive"
other = [n for n in gui.w_node.options if n != gui.node]
if other:
    gui.node = other[0]
    check(gui.index == 0 and status(gui) == "", f"switched to node {gui.node}: {gui.w_index.max + 1} packets")

print("FIGURES", len(gui._views), "views cached; open figs", len(plt.get_fignums()))
print("RESULT", "PASS" if not fails else f"FAIL {len(fails)}: {fails}")
sys.exit(1 if fails else 0)
