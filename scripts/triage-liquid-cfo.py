#!/usr/bin/env python3
"""P0 triage of the pinned-liquidDSP negative-CFO failure (no hardware).

Builds tests/triage/liquid_cfo_sweep.c against the pinned libliquid twice:
  * pinned:    the unmodified static library from scripts/build-modem.py;
  * nco-fixed: the same library with nco_crcf.o replaced by a copy of the
               pinned nco.c whose float->uint32_t phase conversion goes via
               int64_t (well-defined two's-complement wrap).
The pinned checkout is never modified; the patched copy lives in build/triage.
The two curves are separate comparator versions; neither silently replaces the
pinned comparator used by existing studies. Optionally compiles the pinned NCO
with -fsanitize=float-cast-overflow to show the undefined conversion directly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

REVISION = "d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb"
BROKEN = "return (uint32_t)round(_theta * (1LLU<<32) * 0.159154943091895);"
FIXED = "return (uint32_t)(int64_t)round(_theta * (1LLU<<32) * 0.159154943091895);"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--trials", type=int, default=40)
    p.add_argument("--step", type=float, default=0.05)
    p.add_argument("--max-cfo", type=float, default=1.0)
    p.add_argument("--fftw-prefix", type=Path, default=Path("/opt/homebrew") if platform.system() == "Darwin" else Path("/usr"))
    p.add_argument("--no-ubsan", action="store_true", help="skip the sanitizer confirmation build")
    a = p.parse_args()
    if a.trials < 1 or a.step <= 0 or a.max_cfo <= 0:
        p.error("invalid sweep")
    root = Path(__file__).resolve().parents[1]
    liquid = root / "build" / "modem" / "liquid-src"
    archive = liquid / ("libliquid.ar" if platform.system() == "Darwin" else "libliquid.a")
    if not archive.exists():
        raise SystemExit("pinned liquidDSP not built; run scripts/build-modem.py --fetch")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=liquid, text=True).strip()
    if revision != REVISION:
        raise SystemExit("expected pinned liquidDSP " + REVISION + ", found " + revision)
    out = root / "build" / "triage"
    (out / "nco-fixed").mkdir(parents=True, exist_ok=True)
    pinned_nco = liquid / "src" / "nco" / "src" / "nco.c"
    text = pinned_nco.read_text()
    if text.count(BROKEN) != 1:
        raise SystemExit("pinned nco.c does not contain the expected conversion; triage assumptions invalid")
    (out / "nco-fixed" / "nco.c").write_text(text.replace(BROKEN, FIXED))
    (out / "nco-fixed" / "nco_crcf.c").write_text((liquid / "src" / "nco" / "src" / "nco_crcf.c").read_text())
    cc = os.environ.get("CC", "cc")
    include = ["-I" + str(root / "build" / "modem" / "include"), "-I" + str(liquid), "-I" + str(liquid / "include"),
               "-I" + str(a.fftw_prefix / "include")]
    libs = [str(archive), "-L" + str(a.fftw_prefix / "lib"), "-lfftw3f", "-lm"]
    driver = root / "tests" / "triage" / "liquid_cfo_sweep.c"
    builds = {"pinned": [], "nco-fixed": ["-I" + str(out / "nco-fixed"), str(out / "nco-fixed" / "nco_crcf.c")]}
    report = dict(schema=1, hardware_opened=False, platform=platform.platform(), machine=platform.machine(),
                  compiler=subprocess.run([cc, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
                  liquid_revision=revision, pinned_nco_sha256=sha(pinned_nco), driver_sha256=sha(driver),
                  archive_sha256=sha(archive), trials_per_point=a.trials, channel="noiseless, CFO only, 23-sample delay",
                  defect=dict(file="src/nco/src/nco.c", function="nco_crcf_constrain", pinned=BROKEN, corrected=FIXED,
                              explanation="Converting a negative double to uint32_t is undefined behavior in C. "
                                          "arm64 saturates it to 0, so every negative NCO frequency becomes 0; "
                                          "x86_64 compilers usually produce the wrapped value, so the failure is "
                                          "platform-dependent."),
                  versions={})
    for name, extra in builds.items():
        exe = out / ("liquid_cfo_sweep-" + name)
        # The NCO object given explicitly takes precedence over the archive member.
        subprocess.run([cc, "-O2", "-std=gnu99", *include, str(driver), *extra, *libs, "-o", str(exe)], check=True)
        csv = subprocess.run([str(exe), str(a.trials), str(a.step), str(a.max_cfo)], check=True,
                             capture_output=True, text=True).stdout.strip().splitlines()
        header = csv[0].split(",")
        rows = [dict(zip(header, map(float, line.split(",")))) for line in csv[1:]]
        envelope = {}
        for mask in header[1:]:
            passing = [r["cfo_subcarriers"] for r in rows if r[mask] == 1.0]
            envelope[mask] = [min(passing), max(passing)] if passing else None
        report["versions"][name] = dict(rows=rows, full_delivery_cfo_range=envelope)
        print(name, envelope, flush=True)
    if not a.no_ubsan:
        exe = out / "liquid_cfo_sweep-ubsan"
        build = subprocess.run([cc, "-O1", "-std=gnu99", "-fsanitize=float-cast-overflow", *include, str(driver),
                                str(liquid / "src" / "nco" / "src" / "nco_crcf.c"), *libs, "-o", str(exe)],
                               capture_output=True, text=True)
        if build.returncode == 0:
            run = subprocess.run([str(exe), "1", "0.1", "0.1"], capture_output=True, text=True)
            report["ubsan"] = [l for l in run.stderr.splitlines() if "runtime error" in l][:3]
        else:
            report["ubsan"] = "sanitizer build unavailable: " + build.stderr.strip()[-200:]
        print("ubsan:", report["ubsan"], flush=True)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    print(a.output)


if __name__ == "__main__":
    main()
