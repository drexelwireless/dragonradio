#!/bin/bash
# Read-only check of one grid radio node for grid/ota-pipeline.sh (stage 3).
# Usage: ota-node-health.sh WORKSPACE ADDR [COMMIT] [REFERENCE]
# Nodes run the class image (krd26/dragonradio-class-gr310-20261027-v2); the
# workspace is this tree built there (WORKSPACE/dragonradio/build/dragonradio,
# grid/make-ota-workspace-image.sh), so UHD and gr-uhd are the image's.
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
# IMAGE_ONLY=1: no workspace installed; check the image's own UHD and DragonRadio.
if [ "${IMAGE_ONLY:-0}" = 1 ]; then
    echo "image_versions=$(cat /usr/local/share/sdr-class-image/* 2>/dev/null | tr '\n' ' ' | head -c 400)"
    [ -x /usr/local/dragonradio/dragonradio ] && echo "image_dragonradio=yes" || fail "the image has no /usr/local/dragonradio"
else
    commit=$(cat "$ws/dragonradio/COMMIT" 2>/dev/null || echo none)
    echo "commit=$commit"
    [ -z "$want" ] || [ "$commit" = "$want" ] || fail "workspace commit $commit, expected $want"
    [ -x "$ws/dragonradio/build/dragonradio" ] || fail "no radio executable in $ws/dragonradio/build"
    [ -e "$ws/dragonradio/venv/bin/activate_this.py" ] || fail "no venv in $ws/dragonradio"
fi
avail=$(df --output=avail -k /tmp | tail -1)
echo "tmp_free_kb=$avail"
[ "$avail" -gt 2000000 ] || fail "less than 2 GB free in /tmp"
if pgrep -x dragonradio >/dev/null; then
    pgrep -a -x dragonradio | sed 's/^/running: /'
    fail "a dragonradio process is already running"
fi

# The workspace is built against the image's UHD: probe with the image's tool.
probe=/usr/local/bin/uhd_usrp_probe
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
    # External 10 MHz locked and PPS edges arriving (the shared-PPS nodes),
    # with the image's gr-uhd, read-only (no tuning, no stream), with or
    # without a workspace: the workspace uses the image's UHD.
    ref=$(timeout 60 python3 - "$addr" 2>&1 <<'PY'
import sys, time
from gnuradio import uhd
u = uhd.usrp_source(sys.argv[1], uhd.stream_args(cpu_format='fc32', channels=[0]))
old = (u.get_clock_source(0), u.get_time_source(0))
u.set_clock_source('external', 0)
u.set_time_source('external', 0)
locked = 'unknown'
if 'ref_locked' in u.get_mboard_sensor_names(0):
    t0 = time.time()
    while not u.get_mboard_sensor('ref_locked', 0).to_bool() and time.time() - t0 < 2:
        time.sleep(0.05)
    locked = 'yes' if u.get_mboard_sensor('ref_locked', 0).to_bool() else 'no'
edges, last, t0 = 0, u.get_time_last_pps(0).get_real_secs(), time.time()
while time.time() - t0 < 4:
    t = u.get_time_last_pps(0).get_real_secs()
    if t != last:
        edges, last = edges + 1, t
    time.sleep(0.005)
u.set_clock_source(old[0], 0)
u.set_time_source(old[1], 0)
ok = locked in ('yes', 'unknown') and edges >= 2
print(f'mboard={u.get_usrp_info(0).get("mboard_id", "")}')
print(f'ref_locked={locked}')
print(f'pps_edges={edges}')
print(f'restored={old[0]}/{old[1]}')
print(f'reference_ok={"yes" if ok else "no"}')
PY
)
    echo "$ref" | sed 's/^/ref_check: /'
    echo "$ref" | grep -qx "reference_ok=yes" || fail "external reference not locked or no PPS (ref_check lines above)"
fi
exit $status
