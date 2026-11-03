#!/bin/bash
# Run full-stack simulation scenarios (scripts/sim-fullstack.py) in a workspace
# from setup-fullstack-build.sh after build-fullstack.sh.
# Usage: run-sim-fullstack.sh WORKSPACE OUTDIR [sim-fullstack.py options...]
set -euo pipefail
ws=$(realpath "$1"); out=$2; shift 2
export PATH="$ws/deps/usr/bin:$PATH"
# The dragonradio package is used from the source tree, not installed.
export PYTHONPATH="$ws/dragonradio/python/dragonradio:$ws/venv/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# h5py reads the production HDF5 packet logs for ARQ accounting.
"$ws/venv/bin/python" -c 'import h5py' 2>/dev/null || "$ws/venv/bin/python" -m pip install -q h5py
cd "$ws/dragonradio"
mkdir -p "$out"
{
  echo "commit $(cat "$ws/dragonradio/COMMIT" 2>/dev/null || echo unknown)"
  echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus"
  echo "started $(date -Is)"
} > "$out/run-info.txt"
"$ws/venv/bin/python" scripts/sim-fullstack.py --dragonradio "$ws/dragonradio/dragonradio" --outdir "$out" "$@"
