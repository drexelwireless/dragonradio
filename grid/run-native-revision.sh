#!/bin/bash
# Unattended CPU-only native PHY revision benchmark on a provisioned Linux host.
# Usage: run-native-revision.sh SOURCE.tar.gz LIQUID.tar.gz RESULTS_DIR [CORE]
#   SOURCE.tar.gz  git archive of the commit under test (prefix dragonradio/)
#   LIQUID.tar.gz  standalone pinned liquidDSP checkout with generated configure
#   RESULTS_DIR    durable output directory (e.g. on NFS home); must not exist
#   CORE           logical CPU for pinned software timing (default 20)
# No sudo, no system installs, no SDR access, no RF. FFTW comes from unpacked
# Ubuntu packages and NumPy from a private virtualenv in a /tmp workspace.
set -euo pipefail
umask 077
source_bundle=$(realpath "$1"); liquid_bundle=$(realpath "$2"); results=$3; core=${4:-20}
if [ -e "$results" ]; then echo "results directory exists: $results" >&2; exit 2; fi
mkdir -p "$results"
results=$(realpath "$results")
status() { printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$results/STATUS"; }
trap 'rc=$?; if [ $rc -ne 0 ]; then status "FAILED rc=$rc (see run.log)"; touch "$results/FAILED"; fi' EXIT
workspace=$(mktemp -d /tmp/dragonradio-bench-XXXXXX)
status "workspace $workspace host $(hostname) core $core"
cd "$workspace"
mkdir packages deps
( cd packages && apt download libfftw3-dev libfftw3-single3 libfftw3-double3 libfftw3-long3 libfftw3-quad3 >/dev/null 2>&1 \
  || apt download libfftw3-dev libfftw3-single3 >/dev/null )
for package in packages/*.deb; do dpkg-deb -x "$package" deps; done
mkdir prefix
ln -s "$workspace/deps/usr/include" prefix/include
ln -s "$workspace/deps/usr/lib/x86_64-linux-gnu" prefix/lib
export LD_LIBRARY_PATH="$workspace/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
python3 -m venv venv
venv/bin/python -m pip install -q "numpy>=1.24,<3"
PY="$workspace/venv/bin/python"
tar -xzf "$source_bundle"
mkdir liquid-source
tar -xzf "$liquid_bundle" -C liquid-source --strip-components=1
cd dragonradio
status "building native v2, frozen v1 and pinned liquid bridge"
"$PY" scripts/build-phy.py --fftw-prefix "$workspace/prefix"
"$PY" scripts/build-phy.py --variant reference-v1 --fftw-prefix "$workspace/prefix"
"$PY" scripts/build-modem.py --liquid-source "$workspace/liquid-source" --fftw-prefix "$workspace/prefix" > "$results/build-modem.log" 2>&1
[ -e build/modem/liquid-src ] || ln -s "$workspace/liquid-source" build/modem/liquid-src   # triage expects it here
cp build/phy/manifest.json "$results/build-phy-manifest.json"
cp build/modem/manifest.json "$results/build-modem-manifest.json"
"$PY" scripts/baseline-manifest.py --output "$results/manifest.json" > /dev/null
status "running regression suite"
PYTHONPATH=python/dragonradio "$PY" -m unittest discover -s tests/simulation -v > "$results/tests.log" 2>&1
tail -3 "$results/tests.log" | tee -a "$results/STATUS"
status "triage of pinned liquid CFO behavior on this architecture"
"$PY" scripts/triage-liquid-cfo.py --output "$results/liquid-cfo-triage.json" --fftw-prefix "$workspace/prefix" > "$results/triage.log" 2>&1 \
  || status "triage failed (non-fatal; see triage.log)"
workers=$(( $(getconf _NPROCESSORS_ONLN) - 8 ))   # not nproc: it honors OMP_NUM_THREADS=1
status "quality study with $workers workers"
"$PY" scripts/bench-native-revision.py quality --output "$results" --workers "$workers" > "$results/quality.log" 2>&1
status "software study pinned to CPU $core"
"$PY" scripts/bench-native-revision.py software --output "$results" --core "$core" > "$results/software.log" 2>&1
status "DONE"
touch "$results/DONE"
