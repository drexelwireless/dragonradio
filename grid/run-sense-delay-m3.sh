#!/bin/bash
# Step M3 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# transmit-lead sweep on this node's USRP. TRANSMITS: the probe in ping mode
# sends a 128-sample tone burst (fs/8 offset, 16-sample ramps) every 20 ms,
# timed at the device time estimate plus a lead, and counts bursts the radio
# reports late. It also hears its own bursts on its RX antenna, which checks
# that each one left at the requested time. Lead 0 sends untimed bursts.
#
# RF: 1.322 GHz, TX gain 0 dB by default (the script caps it at 20 dB),
# bandwidth at most 5 MHz (rates 1 and 5 MS/s). Only with Kapil's go-ahead.
# Before each cell it stops if STOPFILE exists; the campaign script creates
# it when an OpenISAC reservation appears.
#
# Runs on a node with the GNU Radio 3.10 class image, in a tree built by
# grid/build-in-class-image.sh, as root or through sudo -n (USRP needs
# CAP_SYS_NICE). Results are written node-local, then copied to OUTDIR.
# Usage: run-sense-delay-m3.sh SRC_DIR OUTDIR FAMILY STOPFILE [TX_GAIN] [ADDR]
set -uo pipefail
src=$(realpath "$1"); out=$(realpath -m "$2"); family=$3; stopfile=$4; txg=${5:-0}
[ -x "$src/build/dragonradio" ] || { echo "not built: $src/build/dragonradio" >&2; exit 2; }
case "$family" in
    N210) rxg=20; addr=${6:-addr=192.168.10.2} ;;
    X310) rxg=10; addr=${6:-addr=192.168.40.2} ;;
    *) echo "FAMILY must be N210 or X310" >&2; exit 2 ;;
esac
rates="1e6 5e6"
leads="0.010 0.005 0.003 0.002 0.0015 0.001 0.00075 0.0005 0.0003 0.0002 0.0001 0"
dur=20; period=0.02
if [ "$(id -u)" = 0 ]; then SUDO=; elif sudo -n true 2>/dev/null; then SUDO="sudo -n"; else
    echo "neither root nor passwordless sudo" >&2; exit 16; fi
cd "$src"
mkdir -p "$out"
local_out=$(mktemp -d /tmp/sense-m3.XXXXXX)
{
    echo "commit $(cat COMMIT 2>/dev/null || echo unknown)"
    echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-)"
    echo "family $family addr $addr tx_gain $txg rx_gain $rxg period $period duration $dur"
    echo "started $(date -Is)"
} > "$out/run-info.txt"
status=0
for rate in $rates; do
    for lead in $leads; do
        if [ -e "$stopfile" ]; then echo "STOP file present: stopping before $rate $lead" >> "$out/run-info.txt"; status=3; break 2; fi
        name="ping-$family-r$rate-lead$lead"
        $SUDO env VIRTUAL_ENV="$src/venv" "$src/build/dragonradio" scripts/sense-delay.py --addr "$addr" \
            --mode ping --tx-ok --freq 1.322e9 --rate "$rate" --tx-gain "$txg" --rx-gain "$rxg" \
            --lead "$lead" --period "$period" --burst-len 128 --duration "$dur" \
            --out "$local_out/$name.json" > "$local_out/$name.log" 2>&1
        rc=$?
        echo "$name exit=$rc" >> "$out/run-info.txt"
        [ $rc -eq 0 ] || status=1
        sleep 2
    done
done
$SUDO chown -R "$(id -u):$(id -g)" "$local_out"
cp "$local_out"/* "$out"/ 2>/dev/null; rm -rf "$local_out"
echo "finished $(date -Is) status=$status" >> "$out/run-info.txt"
exit $status
