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
# # Phase 0: a `live` full-stack simulation run from a notebook
#
# This notebook runs one `scripts/sim-fullstack.py` scenario through
# `grid/run-sim-fullstack.sh` as a subprocess. That means the air relay, two
# production radios on the simulated device, and synthetic UDP traffic,
# **in real time**. The whole process tree is pinned with `taskset` to two
# hyperthreads (one physical core) at nice 10. That is the per-student share
# the plan proposes.
#
# The notebook then parses the run's `summary.json` and the receivers'
# `nodeN.json` and plots goodput and latency. Everything is **simulation**:
# simulated device and air, nothing over the air.
#
# "Strict" means the scenario's own pass criteria, which include no late TX
# bursts and no late or dropped RX bursts. `TOLERANT=True` adds the Colab
# probe's settings (`--sim-rx-latency 0.010`,
# `--set slot_send_lead_time=0.010`).

# %% tags=["parameters"]
MODE = "live"
SCENARIO = "clean-arq"
PHY = "native"              # or "liquid" (adds --phy ofdm -c rs8)
CPUS = "4,36"               # a hyperthread pair on grid47 (cpu k and k+32)
NICE = 10
TOLERANT = False
TIMEOUT_S = 240
TREE = "/tmp/drnb/dragonradio"
RUNROOT = "/tmp/drnb/live"   # run directories (HDF5 logs) stay on the node
TAG = "run"
OUTDIR = "~/dragonradio-nb/results/phase0/live"

# %%
import json
import os
import shlex
import signal
import subprocess
import time
from pathlib import Path

import ipywidgets as widgets
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from IPython.display import display

assert MODE == "live"
tag = f"{TAG}-{SCENARIO}-{PHY}-cpu{CPUS.replace(',', '_')}"
rundir = Path(RUNROOT) / tag
outdir = Path(os.path.expanduser(OUTDIR)) / tag
outdir.mkdir(parents=True, exist_ok=True)

radio_args = []
if PHY == "liquid":
    radio_args += ["--phy", "ofdm", "-c", "rs8"]
if TOLERANT:
    radio_args += ["--sim-rx-latency", "0.010", "--set", "slot_send_lead_time=0.010"]
cmd = ["taskset", "-c", CPUS, "nice", "-n", str(NICE),
       "bash", f"{TREE}/grid/run-sim-fullstack.sh", TREE, str(rundir), "--scenario", SCENARIO]
if radio_args:
    cmd += ["--"] + radio_args
print(shlex.join(cmd))

# %% [markdown]
# ## Run
#
# A progress bar tracks the expected wall time: warm-up, traffic, drain and
# about 8 s of start-up and analysis. The subprocess runs in its own session,
# so a timeout kills the whole tree (air relay and radios).

# %%
EXPECTED_S = 4 + 20 + 4 + 8
bar = widgets.FloatProgress(min=0, max=EXPECTED_S, description=f"{SCENARIO}")
label = widgets.Label("starting")
display(widgets.HBox([bar, label]))

t0 = time.time()
log_path = outdir / "runner.log"
with open(log_path, "w") as log:
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    last_print = 0
    timed_out = False
    while proc.poll() is None:
        el = time.time() - t0
        bar.value = min(el, EXPECTED_S)
        label.value = f"{el:.0f} s / ~{EXPECTED_S} s"
        if el - last_print >= 10:
            print(f"  {el:5.0f} s running")
            last_print = el
        if el > TIMEOUT_S:
            os.killpg(proc.pid, signal.SIGKILL)
            timed_out = True
            break
        time.sleep(0.5)
    proc.wait()
wall = time.time() - t0
bar.value = EXPECTED_S
label.value = f"done in {wall:.1f} s, exit {proc.returncode}" + (" (TIMEOUT)" if timed_out else "")
print(label.value)
print(open(log_path).read()[-1500:])

# %% [markdown]
# ## Parse

# %%
summary = json.load(open(rundir / SCENARIO / "summary.json"))
flows = pd.DataFrame(summary["flows"])[["flow", "sent", "delivered", "delivery", "goodput_kbps",
                                        "latency_p50_ms", "latency_p95_ms", "latency_max_ms"]]
device = pd.DataFrame(summary["device"]).T[["tx_bursts", "tx_late", "tx_underflow", "rx_late", "rx_partial_late",
                                            "rx_queue_drops", "rx_overflows"]]
checks = pd.DataFrame(summary["checks"])
late_tx = int(device.tx_late.sum())
late_rx = int((device.rx_late + device.rx_partial_late + device.rx_queue_drops).sum())
result = dict(tag=tag, scenario=SCENARIO, phy=PHY, cpus=CPUS, nice=NICE, tolerant=TOLERANT,
              exit=proc.returncode, timed_out=timed_out, passed=bool(summary["passed"]), wall_s=round(wall, 1),
              late_tx_bursts=late_tx, late_or_dropped_rx=late_rx,
              failed_checks=[c["check"] for c in summary["checks"] if not c["passed"]],
              goodput_kbps={f["flow"]: round(f["goodput_kbps"], 1) for f in summary["flows"]},
              p50_ms={f["flow"]: round(f["latency_p50_ms"], 1) for f in summary["flows"] if f["latency_p50_ms"] is not None})
json.dump(result, open(outdir / "result.json", "w"), indent=1)
display(flows.round(1), device, checks)
print(json.dumps(result, indent=1))

# %% [markdown]
# ## Look
#
# Goodput is computed per 1 s bin from each receiver's arrival times. That
# counts every arrival, duplicates included, and the scenario reports zero
# duplicates. The latency panel shows each flow's p50, p95 and maximum from
# the summary.

# %%
fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 4.5))
for f in summary["flows"]:
    node = json.load(open(rundir / SCENARIO / f"node{f['dst']}.json"))
    rec = node["received"][f"{f['src']}:{f['port']}"]
    arr = np.array(rec["arrivals"])
    if len(arr) == 0:
        continue
    t = arr - arr[0]
    bins = np.arange(0, t.max() + 1, 1.0)
    counts, _ = np.histogram(t, bins)
    a1.step(bins[:-1], counts * f["size"] * 8 / 1e3, where="post", label=f["flow"])
a1.set(xlabel="time since first arrival [s]", ylabel="goodput [kb/s]", title=f"{SCENARIO} ({PHY}), 1 s bins")
a1.grid(True); a1.legend()
x = np.arange(len(flows))
for i, (col, lab) in enumerate([("latency_p50_ms", "p50"), ("latency_p95_ms", "p95"), ("latency_max_ms", "max")]):
    a2.bar(x + (i - 1) * 0.25, flows[col], width=0.25, label=lab)
a2.set_xticks(x, flows.flow)
a2.set(ylabel="one-way latency [ms]", title="Application latency")
a2.grid(True, axis="y"); a2.legend()
fig.tight_layout(); fig.savefig(outdir / "goodput_latency.png", dpi=110)

# %% tags=["check"]
assert not timed_out and proc.returncode == 0, result
assert result["passed"], result["failed_checks"]
print("checks passed:", tag)
