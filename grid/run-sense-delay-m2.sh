#!/bin/bash
# Step M2 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# receive-only sweep of the sensing-delay probe on this node's USRP. Nothing is
# transmitted: the probe runs in rx mode, which never calls burstTX, and the
# TX gain is set to 0 dB.
#
# Runs on a node with the GNU Radio 3.10 class image, in a tree built by
# grid/build-in-class-image.sh. Opening a USRP needs CAP_SYS_NICE, so the
# radio runs as root (directly, or through sudo -n as the OTA runner does).
# It writes to node-local storage, and the results are then copied to OUTDIR
# as the calling user, because root may not write to the shared home.
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
if [ "$(id -u)" = 0 ]; then SUDO=; elif sudo -n true 2>/dev/null; then SUDO="sudo -n"; else
    echo "neither root nor passwordless sudo" >&2; exit 16; fi
cd "$src"
mkdir -p "$out"
local_out=$(mktemp -d /tmp/sense-m2.XXXXXX)
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
        $SUDO env VIRTUAL_ENV="$src/venv" "$src/build/dragonradio" scripts/sense-delay.py --addr "$addr" --mode rx \
            --rate "$rate" --rx-max-samps-factor "$f" --rx-gain "$gain" --tx-gain 0 \
            --duration "$dur" --out "$local_out/$name.json" > "$local_out/$name.log" 2>&1
        rc=$?
        echo "$name exit=$rc" >> "$out/run-info.txt"
        [ $rc -eq 0 ] || status=1
        sleep 2
    done
done
$SUDO chown -R "$(id -u):$(id -g)" "$local_out"
cp "$local_out"/* "$out"/ && rm -rf "$local_out"
echo "finished $(date -Is) status=$status" >> "$out/run-info.txt"
exit $status
