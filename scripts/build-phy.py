#!/usr/bin/env python3
"""Build the non-liquidDSP frame engine (FFTW, no UHD required)."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fftw-prefix", type=Path, default=Path("/opt/homebrew") if platform.system() == "Darwin" else Path("/usr"))
    parser.add_argument("--variant", choices=("current", "reference-v1"), default="current",
                        help="reference-v1 builds the frozen v1 engine (tests/reference_native_v1) into build/phy-v1")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / ("phy" if args.variant == "current" else "phy-v1")
    source = root / ("src/simulation/FastPHY.cc" if args.variant == "current" else "tests/reference_native_v1/FastPHY.cc")
    build.mkdir(parents=True, exist_ok=True)
    output = build / ("libdragonradio_phy.dylib" if platform.system() == "Darwin" else "libdragonradio_phy.so")
    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O3", "-fPIC",
               "-dynamiclib" if platform.system() == "Darwin" else "-shared",
               "-I" + str(args.fftw_prefix / "include"), source,
               "-L" + str(args.fftw_prefix / "lib"), "-lfftw3f", "-o", output]
    with (build / "build.log").open("w") as log:
        subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT, check=True)
    (build / "manifest.json").write_text(json.dumps(dict(library=str(output), command=list(map(str, command)), dependencies=["FFTW3f", "C++ runtime"], liquidDSP=False, UHD=False, variant=args.variant), indent=2) + "\n")
    print("Built", output)


if __name__ == "__main__":
    main()
