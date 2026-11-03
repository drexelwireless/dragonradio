#!/usr/bin/env python3
"""Build the native PHY revision benchmark report (single HTML page).

    report-native-revision.py --run RUN_DIR [--oracle ORACLE_DIR] [--earlier RUN_DIR] --output report.html

Reads the JSON written by bench-native-revision.py and grid/run-native-revision.sh,
derives every summary and gate figure, and injects them into
scripts/templates/native-revision-report.html. No numbers are typed by hand.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "dragonradio"))
from dragonradio.simulation import revision as rv  # noqa: E402


def load(path):
    return json.loads(Path(path).read_text())


def contiguous(points, step=0.05):
    ok = {round(c, 2) for c, p in points if p >= 1.0}
    if 0.0 not in ok:
        return None
    lo = hi = 0.0
    while round(lo - step, 2) in ok:
        lo = round(lo - step, 2)
    while round(hi + step, 2) in ok:
        hi = round(hi + step, 2)
    return [lo, hi]


def software_summary(results):
    kernels = collections.defaultdict(lambda: dict(tx=[], rx=[], p99=[]))
    streams = collections.defaultdict(list)
    for r in results:
        if "tx" in r:
            k = kernels[r["mode"]]
            k["tx"].append(r["tx"]["median_us"])
            k["rx"].append(float(np.mean([v["median_us"] for v in r["rx"].values()])))
            k["p99"].append(max(v["p99_us"] for v in r["rx"].values()))
        else:
            streams[(r["stream"], r["kind"], r["chunk_samples"])].append(r["cpu_ms_per_airtime_second"])
    kernel = {m: dict(tx=float(np.median(v["tx"])), rx=float(np.median(v["rx"])), rx_p99=float(max(v["p99"])),
                      rx_spread=float(np.ptp(v["rx"])), blocks=len(v["tx"])) for m, v in kernels.items()}
    stream = {}
    for (name, kind, chunk), v in streams.items():
        stream.setdefault(name, {}).setdefault(kind, {})[str(chunk)] = dict(
            median=float(np.median(v)), min=float(min(v)), max=float(max(v)), blocks=len(v))
    return kernel, stream


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--run", type=Path, required=True)
    p.add_argument("--oracle", type=Path, help="separate oracle-only run (defaults to RUN)")
    p.add_argument("--earlier", type=Path, help="an earlier complete run, cited for repeatability")
    p.add_argument("--template", type=Path, default=ROOT / "scripts" / "templates" / "native-revision-report.html")
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run = a.run
    meta = load(run / "meta.json")
    smeta = load(run / "software-meta.json")
    manifest = load(run / "manifest.json")
    oracle_rows = load((a.oracle or run) / "oracle.json")["rows"]
    practical = load(run / "practical.json")["rows"]
    cfo_rows = load(run / "cfo.json")["rows"]
    payload_rows = load(run / "payload.json")["rows"]
    software = load(run / "software.json")["results"]
    triage = load(run / "liquid-cfo-triage.json") if (run / "liquid-cfo-triage.json").exists() else None
    tests = (run / "tests.log").read_text().strip().splitlines()
    test_ran = next(l for l in reversed(tests) if l.startswith("Ran "))
    test_status = tests[-1]

    modes, profiles = list(meta["modes"]), meta["profiles"]

    # Practical curves: worst-user PDR with Wilson interval, goodput, Chase use.
    curves = collections.defaultdict(dict)
    for r in practical:
        users = {u: dict(pdr=v["pdr"], ci=v["pdr_wilson95"]) for u, v in r["users"].items()}
        worst_user = min(users, key=lambda u: users[u]["pdr"])
        first = next(iter(r["users"].values()))
        curves[r["mode"]].setdefault(r["profile"], []).append(dict(
            snr=r["snr_db"], pdr=users[worst_user]["pdr"], ci=users[worst_user]["ci"],
            users={u: round(v["pdr"], 5) for u, v in users.items()},
            goodput=r["aggregate_goodput_bps"], chase=first["mean_chase_trials"]))
    for m in curves:
        for prof in curves[m]:
            curves[m][prof].sort(key=lambda x: x["snr"])

    def needed(mode, prof, threshold=0.9):
        return next((x["snr"] for x in curves[mode][prof] if x["pdr"] >= threshold), None)
    required = {m: {prof: needed(m, prof) for prof in profiles} for m in modes}

    snrs = sorted({r["snr_db"] for r in practical})
    fine = [x / 4 for x in range(0, 4 * int(max(snrs)) + 1)]
    theory_per = {mod: [[s, rv.iid_per(mod, rv.ebno_from_sample_snr(s, b))] for s in fine]
                  for mod, b in (("qpsk", 2), ("16qam", 4))}

    # Frame airtime and nominal ceilings (payload bits / frame airtime).
    frames = {}
    for r in practical:
        if r["profile"] == "awgn" and r["snr_db"] == max(snrs):
            users = len(r["users"])
            frames[r["mode"]] = dict(frame_samples=r["frame_samples"], users=users,
                                     nominal_bps=8 * rv.PAYLOAD * users * 1e6 / r["frame_samples"])
    shannon = {str(s): 0.75e6 * math.log2(1 + 10 ** (s / 10) * 128 / 96) for s in (10, 20, 30)}

    # CFO envelopes.
    cfo = collections.defaultdict(dict)
    for r in cfo_rows:
        key = "noiseless" if r["snr_db"] is None else "20dB"
        cfo[r["mode"]].setdefault(key, []).append([r["cfo_subcarriers"], min(r["pdr"].values())])
    envelope = {}
    for m in cfo:
        for key in cfo[m]:
            cfo[m][key].sort()
            envelope.setdefault(m, {})[key] = contiguous(cfo[m][key])

    # Payload sweep.
    payload = collections.defaultdict(dict)
    for r in payload_rows:
        payload[r["mode"]].setdefault(str(int(r["snr_db"])), {})[str(r["payload"])] = dict(
            goodput=r["aggregate_goodput_bps"], pdr=min(u["pdr"] for u in r["users"].values()),
            frame=r["frame_samples"])

    oracle = [dict(mod=r["modulation"], ebno=r["ebno_db"], ber=r["ber"], ci=r["ber_wilson95"], theory=r["theory_ber"],
                   ok=r["theory_consistent_6sigma"], bits=r["bits"], errors=r["bit_errors"]) for r in oracle_rows]
    oracle_theory = {mod: [[e / 4, rv.theory_ber(mod, e / 4)] for e in range(0, 4 * 15 + 1)] for mod in ("qpsk", "16qam")}

    kernel, stream = software_summary(software)

    # Gates declared in docs/native-revision.rst before measurement.
    false_accepts = sum(u["false_accepts"] for r in practical for u in r["users"].values())
    decodes = sum(u["packets"] for r in practical for u in r["users"].values())
    worse = []
    for r in practical:
        if not r["mode"].startswith("v2-ofdm") or r["mode"].endswith("16qam") or r["mode"].endswith("nochase"):
            continue
        ref = next(x for x in practical if x["mode"] == r["mode"].replace("v2", "v1") and x["profile"] == r["profile"]
                   and x["snr_db"] == r["snr_db"])
        for u, v in r["users"].items():
            lo_v1 = ref["users"][u]["pdr_wilson95"][0]
            if ref["users"][u]["delivered"] >= 50 and v["pdr_wilson95"][1] < lo_v1:
                worse.append((r["mode"], r["profile"], r["snr_db"], u))
    s4096 = lambda name, kind: stream[name][kind]["4096"]["median"]
    gate_rows = []
    def gate(name, passed, evidence):
        gate_rows.append(dict(name=name, passed=bool(passed), evidence=evidence))
    gate("Regression suite and QPSK transmit identity", "OK" in test_status,
         "%s on gridgpu01: %s. QPSK v2 frames byte-identical to v1." % (test_ran, test_status))
    gate("No false accepts", false_accepts == 0, "%d false accepts in %s decode attempts across all modes, channels and SNRs." % (false_accepts, f"{decodes:,}"))
    gate("Oracle BER matches theory", all(r["ok"] for r in oracle),
         "%d of %d Eb/N0 points within the 6-sigma check (QPSK and Gray 16-QAM)." % (sum(r["ok"] for r in oracle), len(oracle)))
    env = [envelope[m]["noiseless"] for m in ("v2-ofdm-qpsk", "v2-ofdma-qpsk")]
    gate("CFO range at least +/-1.0 subcarrier", all(e and e[0] <= -1.0 and e[1] >= 1.0 for e in env),
         "v2 delivers every packet from %+.2f to %+.2f subcarrier (v1 %+.2f to %+.2f; liquid %+.2f to %+.2f)." % (
             *envelope["v2-ofdm-qpsk"]["noiseless"], *envelope["v1-ofdm-qpsk"]["noiseless"], *envelope["legacy-qpsk"]["noiseless"]))
    gate("Packet error rate no worse than v1", not worse,
         "No point where v2's upper 95% bound falls below v1's lower bound." if not worse else "%d points worse: %s" % (len(worse), worse[:4]))
    for kind in ("idle", "loaded"):
        for pair in (("v2-ofdm-qpsk", "v1-ofdm-qpsk"), ("v2-ofdma-qpsk", "v1-ofdma-qpsk")):
            v2, v1, liq = s4096(pair[0], kind), s4096(pair[1], kind), s4096("legacy-qpsk", kind)
            label = "OFDM" if "ofdma" not in pair[0] else "OFDMA"
            gate("Stream CPU, %s %s: at most v1/3 and below legacy" % (label, kind), v2 <= v1 / 3 and v2 < liq,
                 "%.1f ms per airtime second versus v1 %.1f (%.1fx) and legacy %.1f, 4096-sample chunks." % (v2, v1, v1 / v2, liq))
    for m, q in (("v2-ofdm-16qam", "v2-ofdm-qpsk"), ("v2-ofdma-16qam", "v2-ofdma-qpsk")):
        g16 = next(x["goodput"] for x in curves[m]["awgn"] if x["snr"] == 30)
        gq = next(x["goodput"] for x in curves[q]["awgn"] if x["snr"] == 30)
        gate("16-QAM goodput at least 1.8x QPSK (%s, 30 dB)" % ("OFDMA" if "ofdma" in m else "OFDM"), g16 / gq >= 1.8 - 1e-9,
             "%.0f versus %.0f kb/s: %.2fx." % (g16 / 1e3, gq / 1e3, g16 / gq))

    repeat = None
    if a.earlier:
        _, earlier_stream = software_summary(load(a.earlier / "software.json")["results"])
        deviations = []
        for name, kinds in stream.items():
            for kind, chunks in kinds.items():
                for chunk, v in chunks.items():
                    e = earlier_stream.get(name, {}).get(kind, {}).get(chunk)
                    if e:
                        deviations.append(abs(v["median"] / e["median"] - 1))
        earlier_rows = {(r["mode"], r["profile"], r["snr_db"]): r for r in load(a.earlier / "practical.json")["rows"]}
        identical = sum(1 for r in practical if (r["mode"], r["profile"], r["snr_db"]) in earlier_rows and
                        all(r["users"][u]["delivered"] == earlier_rows[(r["mode"], r["profile"], r["snr_db"])]["users"][u]["delivered"]
                            for u in r["users"]))
        repeat = dict(run=a.earlier.name, stream_cells=len(deviations),
                      stream_median_abs_dev=float(np.median(deviations)), stream_max_abs_dev=float(max(deviations)),
                      practical_identical=identical, practical_cells=len(practical))
    hosts = manifest.get("host", {})
    data = dict(
        run=run.name, oracle_run=(a.oracle or run).name, earlier_run=a.earlier.name if a.earlier else None,
        commit=manifest["git"]["head"] or run.name.split("-")[-1],  # git archive bundles carry no .git; run IDs end in the short commit
         platform=hosts.get("platform"), cpu=hosts.get("cpu"),
        logical_cpus=hosts.get("logical_cpus"), core=smeta.get("core"), blocks=smeta.get("blocks"),
        started=meta.get("started"), seeds=len(meta["seeds"]), frames_per_seed=meta["frames_per_seed"],
        packets_per_point=len(meta["seeds"]) * meta["frames_per_seed"], payload=rv.PAYLOAD, snrs=snrs,
        modes=meta["modes"], profiles=profiles, curves=curves, required=required, theory_per=theory_per,
        frames=frames, shannon=shannon, cfo=cfo, envelope=envelope, payload_sweep=payload,
        oracle=oracle, oracle_theory=oracle_theory, kernel=kernel, stream=stream, gates=gate_rows,
        triage=dict(pinned=triage["versions"]["pinned"]["full_delivery_cfo_range"]["experiment_mask"],
                    fixed=triage["versions"]["nco-fixed"]["full_delivery_cfo_range"]["experiment_mask"],
                    ubsan=bool(triage.get("ubsan")), machine=triage["machine"]) if triage else None,
        tests=test_ran + " " + test_status, repeat=repeat)
    html = a.template.read_text()
    marker = "/*__REPORT_DATA__*/null"
    if html.count(marker) != 1:
        raise SystemExit("template must contain exactly one data marker")
    payload_json = json.dumps(data, separators=(",", ":"), allow_nan=False).replace("</", "<\\/")
    a.output.write_text(html.replace(marker, payload_json))
    print(a.output, "%.0f KB" % (a.output.stat().st_size / 1024))
    for g in gate_rows:
        print("PASS" if g["passed"] else "FAIL", g["name"], "-", g["evidence"])


if __name__ == "__main__":
    main()
