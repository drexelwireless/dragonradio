#!/bin/bash
# Step M3 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# transmit-lead sweep of the sensing-delay probe on several grid nodes, run
# from gridgpu01. TRANSMITS (grid/run-sense-delay-m3.sh): short bursts at
# 1.322 GHz, TX gain 0 dB by default, at most 5 MHz. Run only with Kapil's
# go-ahead; the standing approvals cover 1.322 GHz, TX gain up to 20 dB and
# 5 MHz, and no transmission while an OpenISAC (oisac-*) reservation is
# active.
#
# Reserves the nodes with the class image (no flash, no DragonRadio service,
# no traffic), checks each node's radio, installs a built tree, then runs the
# nodes ONE AT A TIME, so no node hears another's bursts. A watcher checks
# for an OpenISAC reservation every 30 s and creates OUT/STOP if one appears;
# each node stops before its next 20 s cell. The reservation is released
# whatever happens. Results go to the shared home: OUT/NODE/*.json.
#
# Usage: sense-delay-m3-campaign.sh TREE_TAR OUT NODE:FAMILY... [-- TX_GAIN]
#   TREE_TAR  tar.gz of a tree built at /tmp/drsense/dragonradio by
#             grid/build-in-class-image.sh (paths in its venv are absolute)
#   NODE:FAMILY  e.g. grid08:N210 grid44:X310
# Example:
#   sense-delay-m3-campaign.sh ~/dragonradio-port/ota-ws/ws-sense-XXXXXXXX.tar.gz \
#       ~/dragonradio-sense/m3 grid08:N210 grid01:N210 grid44:X310 grid53:X310
set -uo pipefail
tar=$(realpath "$1"); out=$(realpath -m "$2"); shift 2
specs=(); txg=0
while [ $# -gt 0 ]; do
    if [ "$1" = -- ]; then txg=$2; shift 2; else specs+=("$1"); shift; fi
done
[ ${#specs[@]} -gt 0 ] || { echo "no nodes" >&2; exit 2; }
awk -v g="$txg" 'BEGIN { exit !(g >= 0 && g <= 20) }' || { echo "TX gain $txg outside 0-20 dB" >&2; exit 2; }
HEAD=dwslgrid.ece.drexel.edu; IMAGE=krd26/dragonradio-class-gr310-20261027-v2; KEY=~/.ssh/id_ed25519
WS=/tmp/drsense
mkdir -p "$out"
nodes=$(for s in "${specs[@]}"; do echo -n "${s%%:*} "; done)
log() { echo "$(date -u '+%F %T') $*" | tee -a "$out/campaign.log"; }
gc() { ssh -n -o BatchMode=yes -o ConnectTimeout=60 "$HEAD" gridcli -q "$@"; }
ID=""
nssh() {
    local n=$1; shift
    ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i $KEY -o GSSAPIAuthentication=no \
        -o ConnectTimeout=60 -o ServerAliveInterval=30 -o LogLevel=ERROR \
        -o UserKnownHostsFile="$out/known_hosts.$ID" -o StrictHostKeyChecking=accept-new \
        -o ProxyCommand="ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i $KEY -o LogLevel=ERROR -o ConnectTimeout=60 -W %h:%p $HEAD" \
        "$n" "$@" </dev/null
}
WATCH=""
cleanup() { [ -n "$WATCH" ] && kill "$WATCH" 2>/dev/null; [ -n "$ID" ] && { log "stopping reservation $ID"; gc reservation stop "$ID" >> "$out/campaign.log" 2>&1; }; }
trap cleanup EXIT

[ -r "$tar" ] || { log "no tree tarball $tar"; exit 2; }
gc reservations | grep -q '"oisac-' && { log "OpenISAC reservation active: not starting"; exit 3; }
rm -f "$out/STOP"
name="dr-sense-m3-$(date +%H%M%S)"
log "scheduling $name on $nodes (2h, no flash)"
gc reservation schedule "$IMAGE" $nodes --name "$name" --duration 2h --no-flash \
    --no-dragonradio-service --no-traffic --poll-interval 15 >> "$out/campaign.log" 2>&1
sleep 5
ID=$(gc reservations | sed -n "s/^\([0-9][0-9]*\) \"$name\".*/\1/p" | head -1)
[ -n "$ID" ] || { log "reservation not found"; exit 1; }
log "reservation $ID"
s=""
for i in $(seq 1 40); do s=$(gc reservation info "$ID" 2>&1 | tail -1 | awk '{print $NF}'); [ "$s" = RUNNING ] && break; sleep 30; done
[ "$s" = RUNNING ] || { log "reservation state $s"; exit 1; }
sleep 45

# Reach each node, stop the image's radio service, and check the radio
# without transmitting. An X310 must report FPGA compatibility 19.
ok=()
for spec in "${specs[@]}"; do
    n=${spec%%:*}; fam=${spec##*:}
    case "$fam" in N210) addr=addr=192.168.10.2 ;; X310) addr=addr=192.168.40.2 ;; *) log "$n: bad family $fam"; continue ;; esac
    for t in 1 2 3 4 5 6; do nssh "$n" true && break; sleep 20; done
    nssh "$n" "sudo -n systemctl disable --now dragonradio.service >/dev/null 2>&1; true"
    probe=$(nssh "$n" "timeout 120 uhd_usrp_probe --args=$addr 2>&1" | tee "$out/probe-$n.txt" | grep -m1 -E "Expected FPGA compatibility|RuntimeError|Error:")
    if [ -n "$probe" ]; then log "$n ($fam): radio not usable: $probe"; continue; fi
    if ! nssh "$n" "rm -rf $WS && mkdir -p $WS && tar -xzf '$tar' -C $WS && test -x $WS/dragonradio/build/dragonradio"; then
        log "$n: install failed"; continue
    fi
    log "$n ($fam): ready, tree $(nssh "$n" "cat $WS/dragonradio/COMMIT")"
    ok+=("$spec")
done
[ ${#ok[@]} -gt 0 ] || { log "no usable node"; exit 1; }

# OpenISAC watcher: no transmission while an oisac-* reservation is active.
( while [ ! -e "$out/DONE" ]; do
      if gc reservations | grep -q '"oisac-'; then touch "$out/STOP"; echo "$(date -u '+%F %T') OpenISAC reservation seen: STOP" >> "$out/campaign.log"; fi
      sleep 30
  done ) &
WATCH=$!

log "TX-lead sweep on ${ok[*]}, one node at a time, TX gain $txg dB"
status=0
for spec in "${ok[@]}"; do
    n=${spec%%:*}; fam=${spec##*:}
    [ -e "$out/STOP" ] && { log "STOP: not starting $n"; status=3; break; }
    log "$n ($fam): start"
    nssh "$n" "bash $WS/dragonradio/grid/run-sense-delay-m3.sh $WS/dragonradio '$out/$n' $fam '$out/STOP' $txg" \
        > "$out/run-$n.log" 2>&1 || { status=1; log "$n: sweep reported failures"; }
    log "$n: done"
done
touch "$out/DONE"
log "sweep done, status $status"
exit $status
