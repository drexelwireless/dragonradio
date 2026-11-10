#!/bin/bash
# Step M2 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# receive-only sweep of the sensing-delay probe on this node's USRP. Nothing is
# transmitted: the probe runs in rx mode, which never calls burstTX, and the
# TX gain is set to 0 dB.
#
# Runs on a node with the GNU Radio 3.10 class image, in a tree built by
# grid/build-in-class-image.sh.
# Usage: run-sense-delay-m2.sh SRC_DIR OUTDIR FAMILY [DURATION] [ADDR]
#   FAMILY   N210 or X310 (sets the rates and RX gain)
#   DURATION seconds per cell (default 60)
#   ADDR     UHD address (default from FAMILY)
set -uo pipefail
src=$(realpath "$1"); out=$(realpath -m "$2"); family=$3; dur=${4:-60}
[ -x "$src/build/dragonradio" ] || { echo "not built: $src/build/dragonradio" >&2; exit 2; }
case "$family" in
    N210) rates="1e6 2e6 5e6";      gain=20; addr=${5:-addr=192.168.10.2} ;;
    X310) rates="1e6 2e6 5e6 10e6"; gain=10; addr=${5:-addr=192.168.40.2} ;;
    *) echo "FAMILY must be N210 or X310" >&2; exit 2 ;;
esac
export VIRTUAL_ENV="$src/venv"
cd "$src"
mkdir -p "$out"
{
    echo "commit $(cat COMMIT 2>/dev/null || echo unknown)"
    echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-)"
    echo "family $family addr $addr rx_gain $gain duration $dur"
    echo "started $(date -Is)"
} > "$out/run-info.txt"
status=0
for rate in $rates; do
    for f in 8 4 2 1; do
        name="rx-$family-r$rate-f$f"
        "$src/build/dragonradio" scripts/sense-delay.py --addr "$addr" --mode rx \
            --rate "$rate" --rx-max-samps-factor "$f" --rx-gain "$gain" --tx-gain 0 \
            --duration "$dur" --out "$out/$name.json" > "$out/$name.log" 2>&1
        rc=$?
        echo "$name exit=$rc" >> "$out/run-info.txt"
        [ $rc -eq 0 ] || status=1
        sleep 2
    done
done
echo "finished $(date -Is) status=$status" >> "$out/run-info.txt"
exit $status
