#!/usr/bin/env python3
"""P0 provenance manifest: source tree, three PHY backends, toolchain, host.

Records the git revision, every modified/untracked source file's SHA-256 (the
work is largely uncommitted), the native/legacy/reference backend identities,
dependency versions and host facts that affect timing. Opens no hardware and
changes nothing; build outputs, results and virtualenvs are hashed only where
they are backend libraries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

EXCLUDE = ("build/", "simulation-results/", ".venv-simulation/", "venv/", "results/")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(cmd, cwd=None):
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--checkpoint-ref", default="refs/checkpoints/p0-2026-11-02")
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    git = lambda *args: run(["git", *args], cwd=root)
    status = git("status", "--porcelain=v1", "--untracked-files=all") or ""
    dirty = {}
    for line in status.splitlines():
        code, path = line[:2], line[3:]
        if path.startswith(EXCLUDE) or "__pycache__" in path or path.endswith(".pyc"):
            continue
        file = root / path
        dirty[path] = dict(status=code.strip(), sha256=sha(file) if file.is_file() else None)
    libraries = {}
    for name in ("phy", "modem"):
        for lib in sorted((root / "build" / name).glob("libdragonradio_*")):
            if lib.suffix in (".so", ".dylib"):
                libraries[str(lib.relative_to(root))] = sha(lib)
    manifests = {name: json.loads((root / "build" / name / "manifest.json").read_text())
                 for name in ("phy", "modem") if (root / "build" / name / "manifest.json").exists()}
    liquid = root / "build" / "modem" / "liquid-src"
    reference = sorted((root / "python/dragonradio/dragonradio/simulation").glob("ofdma.py"))
    try:
        import numpy
        numpy_version = numpy.__version__
    except ImportError:
        numpy_version = None
    cpu = (run(["sysctl", "-n", "machdep.cpu.brand_string"]) if platform.system() == "Darwin"
           else next((l.split(":", 1)[1].strip() for l in Path("/proc/cpuinfo").read_text().splitlines()
                      if l.startswith("model name")), None) if Path("/proc/cpuinfo").exists() else None)
    report = dict(
        schema=1, hardware_opened=False,
        git=dict(head=git("rev-parse", "HEAD"), branch=git("rev-parse", "--abbrev-ref", "HEAD"),
                 checkpoint_ref=a.checkpoint_ref, checkpoint=git("rev-parse", "--verify", "-q", a.checkpoint_ref),
                 dirty_file_count=len(dirty), dirty_files=dirty),
        backends={
            "legacy-ofdm": dict(description="original src/liquid OFDM wrappers via src/simulation/LiquidBridge.cc",
                                liquid_revision=run(["git", "rev-parse", "HEAD"], cwd=liquid) if liquid.exists() else None,
                                library_sha256=next((v for k, v in libraries.items() if "modem" in k), None),
                                known_defect="pinned nco.c float->uint32 conversion is undefined; negative CFO "
                                             "fails on arm64 (see scripts/triage-liquid-cfo.py)"),
            "native-ofdm/native-ofdma": dict(description="FFTW engine src/simulation/FastPHYCore.hh + FastPHY.cc C ABI",
                                             core_sha256=sha(root / "src/simulation/FastPHYCore.hh"),
                                             abi_sha256=sha(root / "src/simulation/FastPHY.cc"),
                                             library_sha256=next((v for k, v in libraries.items() if "phy/" in k), None)),
            "reference-python": dict(description="NumPy reference PHY", sources={str(r.relative_to(root)): sha(r) for r in reference}),
        },
        libraries=libraries, build_manifests=manifests,
        toolchain=dict(cxx=(run([os.environ.get("CXX", "c++"), "--version"]) or "").splitlines()[:1],
                       fftw=run(["pkg-config", "--modversion", "fftw3f"]) or run(["brew", "list", "--versions", "fftw"]),
                       python=platform.python_version(), executable=sys.executable, numpy=numpy_version),
        host=dict(platform=platform.platform(), machine=platform.machine(), cpu=cpu, logical_cpus=os.cpu_count(),
                  load_average=os.getloadavg() if hasattr(os, "getloadavg") else None,
                  thread_env={k: os.environ.get(k) for k in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "VECLIB_MAXIMUM_THREADS")},
                  uhd_python=shutil.which("uhd_usrp_probe") is not None),
    )
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(dict(output=str(a.output), head=report["git"]["head"], checkpoint=report["git"]["checkpoint"],
                          dirty_files=len(dirty), libraries=libraries), indent=2))


if __name__ == "__main__":
    main()
