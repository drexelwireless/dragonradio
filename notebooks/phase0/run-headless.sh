#!/bin/bash
# Run a Phase 0 smoke notebook headless on the node: pair/convert the jupytext
# .py to .ipynb, then execute it with papermill on the kernel named in its
# metadata. The executed copy goes to OUTDIR (default
# ~/dragonradio-nb/results/phase0), never into the tree.
#
# Usage: run-headless.sh NOTEBOOK.py [papermill args, e.g. -p N_PACKETS 20]
# Env:   NBVENV (default /tmp/drnb/nbvenv), OUTDIR, TIMEOUT (cell timeout, s)
set -euo pipefail
nbvenv=${NBVENV:-/tmp/drnb/nbvenv}
out=${OUTDIR:-$HOME/dragonradio-nb/results/phase0}
py=$(realpath "$1"); shift
name=$(basename "$py" .py)
mkdir -p "$out"
work=$(mktemp -d /tmp/drnb-nb.XXXXXX)
trap 'rm -rf "$work"' EXIT
"$nbvenv/bin/jupytext" --quiet --to ipynb -o "$work/$name.ipynb" "$py"
start=$(date +%s.%N)
set +e
"$nbvenv/bin/papermill" --no-progress-bar --log-output --cwd "$(dirname "$py")" \
    --execution-timeout "${TIMEOUT:-1800}" "$work/$name.ipynb" "$out/$name.executed.ipynb" "$@"
rc=$?
set -e
printf '%s exit=%d wall=%.1fs executed=%s\n' "$name" "$rc" "$(echo "$(date +%s.%N) - $start" | bc)" "$out/$name.executed.ipynb"
exit $rc
