#!/usr/bin/env python3
"""Package frame engine sources and grid runners; exclude secrets/build outputs."""
import hashlib
import json
from pathlib import Path
import tarfile


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / "build" / "dragonradio-frame-grid.tar.gz"
    output.parent.mkdir(exist_ok=True)
    files = ["Makefile.simulation", "requirements-simulation.txt", "requirements-benchmark.txt", "LICENSE", "README.md", "CLAUDE_FULLSTACK_BENCHMARK_PLAN.md", "tests/reference_stream_v1.py"]
    directories = ["python/dragonradio/dragonradio/simulation", "src/simulation", "tests/simulation", "tests/reference_phy_v1", "grid"]
    files += ["scripts/" + name for name in ("build-phy.py", "build-modem.py", "benchmark-phy.py", "frame-radio.py", "sim-radio.py", "standalone-radio.py", "package-grid.py", "plot-phy-benchmark.py", "ota-phy.py", "summarize-ota.py", "plot-ota-demo.py", "characterize-phy.py", "plot-characterization.py", "compare-software.py")]
    files += ["docs/" + name for name in ("simulation.rst", "native-phy.rst", "native-phy-evaluation.rst", "ota-grid.rst", "images/phy-benchmark.png", "images/ota-demo.png", "characterization.rst", "images/characterization.png")]
    # The liquidDSP comparison must retain the original wrappers and dependency
    # source identifiers. No compiled binaries are copied between architectures.
    files += ["src/" + name for name in ("Header.hh", "Seq.hh", "liquid/OFDM.hh", "liquid/OFDM.cc", "liquid/Modem.hh", "liquid/Modem.cc", "liquid/Mutex.hh", "liquid/Mutex.cc", "phy/Modem.hh", "dsp/FFTW.hh", "dsp/FFTW.cc")]
    for directory in directories:
        files.extend(str(p.relative_to(root)) for p in (root / directory).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts and p.suffix not in (".pyc", ".so", ".dylib"))
    hashes = {}
    with tarfile.open(output, "w:gz") as archive:
        for relative in sorted(set(files)):
            path = root / relative
            if not path.is_file():
                raise FileNotFoundError(path)
            hashes[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
            archive.add(path, arcname="dragonradio-frame/" + relative, recursive=False)
    manifest = dict(archive=output.name, sha256=hashlib.sha256(output.read_bytes()).hexdigest(), files=hashes,
                    architecture="sources; compile on destination", hardware_opened=False)
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(output)


if __name__ == "__main__":
    main()
