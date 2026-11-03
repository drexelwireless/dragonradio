#!/usr/bin/env python3
"""Build existing DragonRadio modem wrappers without UHD; no system install."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

REVISION = "d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--liquid-source", type=Path)
    parser.add_argument("--fetch", action="store_true", help="Fetch pinned liquidDSP into build/ if absent")
    parser.add_argument("--fftw-prefix", type=Path, default=Path("/opt/homebrew") if platform.system() == "Darwin" else Path("/usr"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / "modem"
    build.mkdir(parents=True, exist_ok=True)
    source = args.liquid_source.resolve() if args.liquid_source else build / "liquid-src"
    with (build / "build.log").open("w") as log:
        def run(cmd, cwd=root, env=None):
            print("Running:", " ".join(map(str, cmd)), flush=True)
            log.write("\n$ " + " ".join(map(str, cmd)) + "\n")
            log.flush()
            subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        if not (source / ".git").exists() and not source.exists() and args.fetch:
            run(["git", "clone", "--no-checkout", "https://github.com/drexelwireless/liquid-dsp.git", source])
            run(["git", "checkout", "--detach", REVISION], cwd=source)
        if not (source / "configure.ac").exists():
            raise RuntimeError("liquidDSP source absent; supply --liquid-source or --fetch")
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        if revision != REVISION:
            raise RuntimeError("benchmark requires DragonRadio's pinned liquidDSP revision " + REVISION)
        environment = os.environ.copy()
        environment["CPPFLAGS"] = "-I" + str(args.fftw_prefix / "include")
        environment["LDFLAGS"] = "-L" + str(args.fftw_prefix / "lib")
        environment["CFLAGS"] = "-O3 -std=gnu99"
        if not (source / "configure").exists():
            run(["sh", "bootstrap.sh"], cwd=source, env=environment)
        run(["./configure", "--prefix=" + str(build / "prefix")], cwd=source, env=environment)
        run(["make", "-j4"], cwd=source, env=environment)
        # Header layout expected by the original DragonRadio source.
        include = build / "include" / "liquid"
        include.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / "include" / "liquid.h", include / "liquid.h")
        library = source / ("libliquid.ar" if platform.system() == "Darwin" else "libliquid.a")
        output = build / ("libdragonradio_modem.dylib" if platform.system() == "Darwin" else "libdragonradio_modem.so")
        sources = [root / p for p in ("src/simulation/LiquidBridge.cc", "src/liquid/OFDM.cc", "src/liquid/Modem.cc", "src/liquid/Mutex.cc", "src/dsp/FFTW.cc")]
        run([os.environ.get("CXX", "c++"), "-std=c++17", "-O3", "-fPIC",
             "-dynamiclib" if platform.system() == "Darwin" else "-shared",
             "-I" + str(root / "src"), "-I" + str(build / "include"),
             "-I" + str(args.fftw_prefix / "include"), *sources, library,
             "-L" + str(args.fftw_prefix / "lib"), "-lfftw3f", "-lpthread", "-o", output])
        manifest = dict(liquid_revision=revision, library=str(output), compiler=os.environ.get("CXX", "c++"),
                        fftw_prefix=str(args.fftw_prefix), coding="CRC32, uncoded QPSK header and payload",
                        scope="Existing src/liquid/OFDM.hh wrappers; no UHD/MAC/TAP")
        (build / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print("Built", output)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(str(exc) + "; see build/modem/build.log", file=sys.stderr)
        raise SystemExit(1)
