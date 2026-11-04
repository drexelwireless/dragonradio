#!/bin/bash
# Run full-stack simulation scenarios (scripts/sim-fullstack.py) on a node
# running the GNU Radio 3.10 class image, in a tree built by
# grid/build-in-class-image.sh. Every radio uses the simulated device
# (--sim-air); no USRP is opened.
#
# Usage: run-sim-fullstack.sh SRC_DIR OUTDIR [sim-fullstack.py options...]
#   SRC_DIR  the built tree (SRC_DIR/build/dragonradio, SRC_DIR/venv)
#   OUTDIR   results: OUTDIR/SCENARIO/{summary.json,node*.json,node*.log,...},
#            OUTDIR/summary.json and OUTDIR/run-info.txt
# Examples:
#   run-sim-fullstack.sh /tmp/drport/dragonradio /tmp/drport/sim/native
#   run-sim-fullstack.sh /tmp/drport/dragonradio /tmp/drport/sim/liquid -- --phy ofdm -c rs8
#   run-sim-fullstack.sh /tmp/drport/dragonradio /tmp/drport/sim/one --scenario clean-arq
set -euo pipefail
src=$(realpath "$1"); out=$2; shift 2
[ -x "$src/build/dragonradio" ] || { echo "not built: $src/build/dragonradio" >&2; exit 2; }
# The radio's embedded Python uses this tree's venv (activate_this.py): this
# tree's dragonradio package first, then the image venv's packages (h5py too).
export VIRTUAL_ENV="$src/venv"
mkdir -p "$out"
{
  echo "commit $(cat "$src/COMMIT" 2>/dev/null || echo unknown)"
  echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus"
  echo "image $(ls -d /usr/local/dragonradio 2>/dev/null || echo none)"
  echo "started $(date -Is)"
} > "$out/run-info.txt"
cd "$src"
exec "$src/venv/bin/python" scripts/sim-fullstack.py --dragonradio "$src/build/dragonradio" --outdir "$out" "$@"
