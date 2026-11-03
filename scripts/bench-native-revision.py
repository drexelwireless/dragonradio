#!/usr/bin/env python3
"""Native PHY revision benchmark: quality (parallel) and software (pinned) studies.

    bench-native-revision.py quality  --output DIR [--workers N] [--quick]
    bench-native-revision.py software --output DIR [--core C] [--quick]

Simulation only; never imports UHD or opens a radio. Each study writes its own
JSON as soon as it finishes, so partial runs keep completed evidence.
"""
import argparse
import json
import multiprocessing
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import time

for name in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "VECLIB_MAXIMUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(name, "1")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "dragonradio"))

from dragonradio.simulation import revision as rv  # noqa: E402


def log(*args):
    print(time.strftime("%H:%M:%S"), *args, flush=True)


def save(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=1) + "\n")
    tmp.replace(path)


def run_pool(pool, function, tasks, label):
    start, results = time.time(), []
    for i, r in enumerate(pool.imap_unordered(function, tasks, chunksize=1), 1):
        results.append(r)
        if i % max(1, len(tasks) // 20) == 0 or i == len(tasks):
            log("%s: %d/%d tasks (%.0f s)" % (label, i, len(tasks), time.time() - start))
    return results


def quality(a):
    out = a.output
    out.mkdir(parents=True, exist_ok=True)
    quick = a.quick
    seeds = list(range(2 if quick else a.seeds))
    frames = 20 if quick else 100
    snrs = [0, 10, 20] if quick else list(range(0, 32, 2))
    modes = list(rv.MODES)
    profiles = ["awgn", "cfo-0.6"] if quick else list(rv.PROFILES)
    meta = dict(provenance=rv.provenance(ROOT), studies=a.studies, seeds=seeds, frames_per_seed=frames, snrs_db=snrs,
                modes=rv.MODES, profiles=profiles, payload=rv.PAYLOAD, quick=quick, workers=a.workers,
                hardware_opened=False, started=time.strftime("%Y-%m-%dT%H:%M:%S%z"))
    save(out / "meta.json", meta)
    ctx = multiprocessing.get_context("fork" if sys.platform.startswith("linux") else "spawn")
    with ctx.Pool(a.workers, maxtasksperchild=200) as pool:
        # 1. Oracle BER (known timing/channel) versus theory.
        ebnos = [0, 4, 8] if quick else [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
        tasks = [(mod, e, s, frames) for mod in ("qpsk", "16qam") for e in ebnos for s in seeds]
        if "oracle" in a.studies:
            cells = run_pool(pool, rv.oracle_cell, tasks, "oracle")
            save(out / "oracle.json", dict(meta=meta, rows=rv.aggregate_oracle(cells)))
        # 2. CFO acquisition envelope.
        cfos = [round(x * 0.05, 2) for x in range(-30, 31)] if not quick else [-1.0, -0.5, 0.0, 0.5, 1.0]
        cfo_modes = ["legacy-qpsk", "v1-ofdm-qpsk", "v1-ofdma-qpsk", "v2-ofdm-qpsk", "v2-ofdma-qpsk", "v2-ofdm-16qam", "legacy-16qam"]
        tasks = [(m, c, snr, 20 if quick else 200) for m in cfo_modes for c in cfos for snr in (None, 20.0)]
        if "cfo" in a.studies:
            save(out / "cfo.json", dict(meta=meta, rows=run_pool(pool, rv.cfo_cell, tasks, "cfo")))
        # 3. Practical PER and goodput.
        tasks = [(m, p, snr, s, frames, rv.PAYLOAD) for m in modes for p in profiles for snr in snrs for s in seeds]
        random.Random(1).shuffle(tasks)   # balance load across workers
        rows = []
        if "practical" in a.studies:
            cells = run_pool(pool, rv.practical_cell, tasks, "practical")
            rows = rv.aggregate_practical(cells)
            save(out / "practical.json", dict(meta=meta, rows=rows))
        # 4. Payload sweep (AWGN, robust and waterfall points).
        payload_modes = ["legacy-qpsk", "legacy-16qam", "v2-ofdm-qpsk", "v2-ofdma-qpsk", "v2-ofdm-16qam", "v2-ofdma-16qam"]
        payloads = [64, 256] if quick else [64, 256, 1024, 1500]
        tasks = [(m, "awgn", snr, s, max(10, frames // 2), size) for m in payload_modes for size in payloads
                 for snr in (12.0, 20.0, 30.0) for s in seeds[:5]]
        if "payload" in a.studies:
            cells = run_pool(pool, rv.practical_cell, tasks, "payload")
            save(out / "payload.json", dict(meta=meta, rows=rv.aggregate_practical(cells)))
    gates = {}
    if "oracle" in a.studies:
        gates["oracle_theory_consistent"] = all(r["theory_consistent_6sigma"] for r in json.loads((out / "oracle.json").read_text())["rows"])
    if rows:
        gates["no_false_accepts"] = all(u["false_accepts"] == 0 for r in rows for u in r["users"].values())
    meta["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    meta["gates"] = gates
    save(out / "meta.json", meta)
    log("quality gates", gates)


def software(a):
    out = a.output
    out.mkdir(parents=True, exist_ok=True)
    quick = a.quick
    taskset = shutil.which("taskset")
    prefix = [taskset, "-c", str(a.core)] if taskset and a.core is not None else []
    kernels = ["legacy-qpsk", "legacy-16qam", "v1-ofdm-qpsk", "v1-ofdma-qpsk", "v2-ofdm-qpsk", "v2-ofdma-qpsk",
               "v2-ofdm-16qam", "v2-ofdma-16qam", "v2-ofdm-qpsk-nochase"]
    streams = list(rv.STREAMS)
    chunks = [4096] if quick else [512, 4096, 65536]
    blocks = 1 if quick else 3
    jobs = []
    for block in range(blocks):
        order = [("kernel", k, None, None) for k in kernels]
        order += [("stream", s, kind, c) for s in streams for kind in ("idle", "loaded") for c in chunks]
        random.Random(100 + block).shuffle(order)
        jobs += [(block,) + j for j in order]
    meta = dict(provenance=rv.provenance(ROOT), core=a.core, taskset=bool(prefix), blocks=blocks, chunks=chunks,
                quick=quick, hardware_opened=False, started=time.strftime("%Y-%m-%dT%H:%M:%S%z"))
    save(out / "software-meta.json", meta)
    results = []
    for i, (block, kind, name, stream_kind, chunk) in enumerate(jobs, 1):
        target = out / "workers" / ("%02d-%03d.json" % (block, i))
        target.parent.mkdir(exist_ok=True)
        command = prefix + [sys.executable, __file__, "worker", "--output", str(target), "--kind", kind, "--name", name]
        if kind == "stream":
            command += ["--stream-kind", stream_kind, "--chunk", str(chunk)]
        if quick:
            command.append("--quick")
        subprocess.run(command, check=True)
        r = json.loads(target.read_text())
        r["block"] = block
        results.append(r)
        log("software %d/%d %s %s %s %s" % (i, len(jobs), kind, name, stream_kind or "", chunk or ""))
        save(out / "software.json", dict(meta=meta, results=results))
    meta["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    save(out / "software.json", dict(meta=meta, results=results))


def worker(a):
    if a.kind == "kernel":
        r = rv.kernel_timing(a.name, iterations=300 if a.quick else 3000)
    else:
        r = rv.stream_timing(a.name, a.stream_kind, a.chunk, seconds=0.3 if a.quick else 2.0,
                             repetitions=2 if a.quick else 5)
    r["affinity"] = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    a.output.write_text(json.dumps(r, indent=1) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    q = sub.add_parser("quality")
    q.add_argument("--output", type=Path, required=True)
    q.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 4))
    q.add_argument("--seeds", type=int, default=20)
    q.add_argument("--quick", action="store_true")
    q.add_argument("--studies", nargs="+", choices=("oracle", "cfo", "practical", "payload"),
                   default=["oracle", "cfo", "practical", "payload"])
    s = sub.add_parser("software")
    s.add_argument("--output", type=Path, required=True)
    s.add_argument("--core", type=int)
    s.add_argument("--quick", action="store_true")
    w = sub.add_parser("worker")
    w.add_argument("--output", type=Path, required=True)
    w.add_argument("--kind", choices=("kernel", "stream"), required=True)
    w.add_argument("--name", required=True)
    w.add_argument("--stream-kind", choices=("idle", "loaded"))
    w.add_argument("--chunk", type=int)
    w.add_argument("--quick", action="store_true")
    a = p.parse_args()
    {"quality": quality, "software": software, "worker": worker}[a.command](a)


if __name__ == "__main__":
    main()
