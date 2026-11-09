#!/bin/bash
# How many simultaneous full-stack simulations fit on one host?
# Runs N copies of one scenario at once (each copy is one student's 2-node run:
# air relay + 2 dragonradio processes) and reports each copy's pass/fail,
# late bursts and lead time.
# Usage: concurrency-probe.sh DRSIM CPULIST OUT N [N ...]
#   DRSIM    unpacked bundle from build-colab-bundle.sh (drsim/)
#   CPULIST  taskset list for every process, e.g. 0-15,36-51 to emulate 32 threads
#   OUT      results directory
# Environment: SCENARIO (default clean-arq), NICE (default 5).
set -euo pipefail
drsim=$(realpath "$1"); cpus=$2; out=$(realpath -m "$3"); shift 3
scenario=${SCENARIO:-clean-arq}; nice=${NICE:-5}
. "$drsim/env.sh"
mkdir -p "$out"
for n in "$@"; do
  d="$out/n$n"; rm -rf "$d"; mkdir -p "$d"
  echo "== $n concurrent ($scenario, cpus $cpus) $(date -Is)"
  for i in $(seq 1 "$n"); do
    ( cd "$drsim/dragonradio" && taskset -c "$cpus" nice -n "$nice" python3 scripts/sim-fullstack.py \
        --dragonradio "$drsim/bin/dragonradio" --outdir "$d/r$i" --scenario "$scenario" > "$d/r$i.log" 2>&1 ) &
  done
  wait
  python3 - "$d" "$scenario" <<'EOF'
import json, sys
from pathlib import Path
import pandas as pd
d, sc = Path(sys.argv[1]), sys.argv[2]
rows = []
for r in sorted(d.glob('r*/'), key=lambda p: int(p.name[1:])):
    p = r / sc / 'summary.json'
    if not p.exists():
        rows.append({'run': r.name, 'passed': False, 'note': 'no summary'}); continue
    s = json.loads(p.read_text()); dev = s.get('device', {})
    b = pd.read_csv(r / sc / 'bursts.csv')
    rows.append({'run': r.name, 'passed': s['passed'],
                 'tx_late': sum(v['tx_late'] for v in dev.values()),
                 'rx_partial_late': sum(v.get('rx_partial_late', 0) for v in dev.values()),
                 'relay_late': int((b.lead < 0).sum()),
                 'frame_delivery_min': min((l['frame_delivery'] for l in s['links_accounting'].values()), default=None),
                 'lead_min_ms': round(1e3*b.lead.min(), 2), 'lead_median_ms': round(1e3*b.lead.median(), 2)})
t = pd.DataFrame(rows)
print(t.to_string(index=False))
print(f"passed {int(t.passed.sum())}/{len(t)}")
t.to_csv(d / 'table.csv', index=False)
EOF
done
