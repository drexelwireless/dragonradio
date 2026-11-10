#!/bin/bash
# Phase 0 live-mode capacity check on the node: run 04_live_sim headless
# 5 times in a row (strict defaults), then 4 copies at once on disjoint
# hyperthread pairs. Every ~10 s a sampler records the CPU affinity, current
# CPU and nice value of each radio, air relay and runner process.
# Results: OUTDIR (default ~/dragonradio-nb/results/phase0/live-batch).
#
# Usage: run-live-batch.sh [PHY]   (native or liquid; default native)
set -uo pipefail
here=$(dirname "$(realpath "$0")")
phy=${1:-native}
out=${OUTDIR:-$HOME/dragonradio-nb/results/phase0/live-batch}
mkdir -p "$out"
sample() {  # sample LABEL: affinity of every simulation process, every 10 s, until stopped
    while sleep 10; do
        echo "== $1 $(date -Is)"
        for p in $(pgrep -u "$USER" -f 'sim-node.py|sim-air.py|sim-fullstack.py'); do
            printf '%s cpus=%s psr=%s ni=%s %s\n' "$p" "$(taskset -cp "$p" 2>/dev/null | sed 's/.*: //')" \
                "$(ps -o psr= -p "$p")" "$(ps -o ni= -p "$p")" "$(ps -o args= -p "$p" | cut -c1-90)"
        done
    done
}
run() {  # run TAG CPUS
    OUTDIR=$out RUN_NAME=$1 bash "$here/run-headless.sh" "$here/04_live_sim.py" -p TAG "$1" -p CPUS "$2" -p PHY "$phy" \
        -p OUTDIR "$out" > "$out/$1.log" 2>&1
    echo "$1 cpus=$2 exit=$?"
}
sample sequential > "$out/affinity-sequential-$phy.log" 2>&1 & s=$!
for i in 1 2 3 4 5; do run "seq$i-$phy" 4,36; done
kill $s
sample concurrent > "$out/affinity-concurrent-$phy.log" 2>&1 & s=$!
run "par1-$phy" 8,40 & run "par2-$phy" 9,41 & run "par3-$phy" 10,42 & run "par4-$phy" 11,43 &
wait $(jobs -p | grep -v "^$s$")
kill $s
