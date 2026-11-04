#!/bin/bash
# Read-only check of one grid radio node for grid/ota-pipeline.sh (stage 3).
# Usage: ota-node-health.sh WORKSPACE ADDR [COMMIT] [REFERENCE]
# Prints key=value lines and the radio probe. Opens the radio only through
# uhd_usrp_probe and, with REFERENCE=external, scripts/usrp-ref-check.py
# (read-only: no tuning, no stream, no transmission). Exits non-zero on a
# problem that rules the node out.
set -uo pipefail
ws=$1; addr=$2; want=${3:-}; reference=${4:-}
status=0
fail() { echo "FAIL $*"; status=1; }

echo "host=$(hostname)"
echo "os=$(. /etc/os-release && echo "$ID-$VERSION_ID")"
echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs) nproc=$(nproc)"
ntp=$(timedatectl show -p NTPSynchronized --value 2>/dev/null || echo unknown)
echo "ntp=$ntp"
# A container shares its host's kernel clock, so timedatectl inside it may
# report "no" while the host is synchronized. Warn only: the trial start time
# tolerates seconds, and the radio's timesync removes any whole-second
# disagreement in the PPS time within its first fit.
[ "$ntp" = yes ] || echo "WARN host clock not reported NTP-synchronized"
if [ "$(id -u)" = 0 ]; then echo "privilege=root"
elif sudo -n true 2>/dev/null; then echo "privilege=sudo"
else fail "neither root nor passwordless sudo (the radio needs CAP_NET_ADMIN for its tap)"; fi
[ -c /dev/net/tun ] && echo "tun=yes" || fail "/dev/net/tun missing"
commit=$(cat "$ws/dragonradio/COMMIT" 2>/dev/null || echo none)
echo "commit=$commit"
[ -z "$want" ] || [ "$commit" = "$want" ] || fail "workspace commit $commit, expected $want"
[ -x "$ws/dragonradio/dragonradio" ] || fail "no radio executable in $ws"
avail=$(df --output=avail -k /tmp | tail -1)
echo "tmp_free_kb=$avail"
[ "$avail" -gt 2000000 ] || fail "less than 2 GB free in /tmp"
! pgrep -x dragonradio >/dev/null || fail "a dragonradio process is already running"

export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial"
probe="$ws/deps/usr/bin/uhd_usrp_probe"
[ -x "$probe" ] || probe=$(command -v uhd_usrp_probe || true)
[ -n "$probe" ] || { fail "no uhd_usrp_probe"; exit $status; }
host_ip=${addr#addr=}
if command -v ping >/dev/null; then
    ping -c1 -W2 "$host_ip" >/dev/null 2>&1 && echo "radio_ping=yes" || fail "radio $host_ip does not answer ping"
else
    echo "radio_ping=skipped (no ping); the probe below checks the radio"
fi
out=$(timeout 120 "$probe" --args "$addr" 2>&1)
echo "$out" | grep -E "UHD|Mboard:|Device:|FPGA Version|FW Version|Time sources|Clock sources|ref_locked|Sensors|serial" | sort -u | head -30
if echo "$out" | grep -qi "compatib"; then
    echo "$out" | grep -i -m3 "compatib"
    fail "FPGA/firmware not compatible with this UHD; do not flash without approval"
fi
echo "$out" | grep -q "Mboard:" || fail "probe found no motherboard at $addr"
if [ "$reference" = external ]; then
    # External 10 MHz locked and PPS edges arriving (the shared-PPS nodes).
    ref=$(env PYTHONPATH="$ws/dragonradio/python/dragonradio:$ws/venv/lib/python3.12/site-packages" \
          timeout 60 "$ws/dragonradio/dragonradio" "$ws/dragonradio/scripts/usrp-ref-check.py" --addr "$addr" 2>&1)
    echo "$ref" | sed 's/^/ref_check: /'
    echo "$ref" | grep -qx "reference_ok=yes" || fail "external reference not locked or no PPS (ref_check lines above)"
fi
exit $status
