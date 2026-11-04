#!/bin/bash
# Real-time soak of the production radio over the simulated device (P4).
# Usage: run-sim-soak.sh WORKSPACE OUTDIR [sim-soak.py options...] [-- radio options]
# See scripts/sim-soak.py and docs/performance.rst.
set -euo pipefail
ws=$(realpath "$1"); out=$2; shift 2
export PATH="$ws/deps/usr/bin:$PATH"
export PYTHONPATH="$ws/dragonradio/python/dragonradio:$ws/venv/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$ws/venv/bin/python" -c 'import h5py, numpy' 2>/dev/null || "$ws/venv/bin/python" -m pip install -q h5py numpy
cd "$ws/dragonradio"
mkdir -p "$out"
{
  echo "commit $(git rev-parse HEAD 2>/dev/null || cat COMMIT 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo ' (dirty)')"
  echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus; load $(cat /proc/loadavg)"
  echo "started $(date -Is)"
} > "$out/run-info.txt"
"$ws/venv/bin/python" scripts/sim-soak.py --dragonradio "$ws/dragonradio/dragonradio" --outdir "$out" "$@"
status=$?
echo "finished $(date -Is) status $status load $(cat /proc/loadavg)" >> "$out/run-info.txt"
exit $status
