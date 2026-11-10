#!/bin/bash
# Step M4 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# one side of one reaction-test cell on this node's USRP. TRANSMITS. The
# campaign script (grid/sense-delay-m4-campaign.sh) runs the react side on
# node B, waits for it to calibrate its noise floor, then runs the ping side
# on node A. A sends a 128-sample tone burst every PERIOD; B replies to each
# energy edge it detects with the same burst, timed at its device time
# estimate plus LEAD, or untimed when LEAD is 0. A hears its own burst and
# B's reply on its own RX antenna, so A measures the end-to-end sensing
# delay D on its own clock.
#
# RF: 1.322 GHz, TX gain TXG (capped at 20 dB here and in the probe), rates
# 1 and 5 MS/s only. Only with Kapil's go-ahead.
#
# Runs as root or through sudo -n (USRP needs CAP_SYS_NICE), in a tree built
# by grid/build-in-class-image.sh. Writes node-local, then copies NAME.json
# and NAME.log to OUTDIR as the calling user.
# Usage: run-sense-delay-m4-cell.sh SRC_DIR OUTDIR FAMILY ROLE NAME RATE FACTOR LEAD PERIOD DURATION TXG
#   ROLE   react (node B) or ping (node A)
#   FACTOR rx_max_samps_factor (receive chunk = FACTOR packets)
set -uo pipefail
[ $# -eq 11 ] || { echo "usage: $0 SRC OUT FAMILY ROLE NAME RATE FACTOR LEAD PERIOD DURATION TXG" >&2; exit 2; }
src=$(realpath "$1"); out=$(realpath -m "$2"); family=$3; role=$4; name=$5
rate=$6; factor=$7; lead=$8; period=$9; dur=${10}; txg=${11}
[ -x "$src/build/dragonradio" ] || { echo "not built: $src/build/dragonradio" >&2; exit 2; }
case "$family" in
    N210) rxg=20; addr=addr=192.168.10.2 ;;
    X310) rxg=10; addr=addr=192.168.40.2 ;;
    *) echo "FAMILY must be N210 or X310" >&2; exit 2 ;;
esac
case "$role" in react|ping) ;; *) echo "ROLE must be react or ping" >&2; exit 2 ;; esac
case "$rate" in 1e6|5e6) ;; *) echo "RATE must be 1e6 or 5e6 (5 MHz approval)" >&2; exit 2 ;; esac
awk -v g="$txg" 'BEGIN { exit !(g >= 0 && g <= 20) }' || { echo "TX gain $txg outside 0-20 dB" >&2; exit 2; }
if [ "$(id -u)" = 0 ]; then SUDO=; elif sudo -n true 2>/dev/null; then SUDO="sudo -n"; else
    echo "neither root nor passwordless sudo" >&2; exit 16; fi
cd "$src"
mkdir -p "$out"
local_out=$(mktemp -d /tmp/sense-m4.XXXXXX)
$SUDO env VIRTUAL_ENV="$src/venv" "$src/build/dragonradio" scripts/sense-delay.py --addr "$addr" \
    --mode "$role" --tx-ok --freq 1.322e9 --rate "$rate" --tx-gain "$txg" --rx-gain "$rxg" \
    --rx-max-samps-factor "$factor" --lead "$lead" --period "$period" --burst-len 128 \
    --duration "$dur" --out "$local_out/$name.json" > "$local_out/$name.log" 2>&1
rc=$?
$SUDO chown -R "$(id -u):$(id -g)" "$local_out"
cp "$local_out"/* "$out"/ 2>/dev/null; rm -rf "$local_out"
echo "$(date -Is) $name exit=$rc" >> "$out/cells.txt"
exit $rc
