#!/bin/bash
# Step M2 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# receive-only sweep of the sensing-delay probe on several grid nodes, run
# from gridgpu01. Nothing is transmitted (grid/run-sense-delay-m2.sh).
#
# Reserves the nodes with the class image (no flash, no DragonRadio service,
# no traffic), checks each node's radio, installs a built tree, runs the sweep
# on every node at once, and releases the reservation whatever happens.
# Results go straight to the shared home: OUT/NODE/*.json.
#
# Usage: sense-delay-m2-campaign.sh TREE_TAR OUT NODE:FAMILY... [-- DURATION]
#   TREE_TAR  tar.gz of a tree built at /tmp/drsense/dragonradio by
#             grid/build-in-class-image.sh (paths in its venv are absolute)
#   NODE:FAMILY  e.g. grid08:N210 grid44:X310
# Example:
#   sense-delay-m2-campaign.sh ~/dragonradio-port/ota-ws/ws-sense-XXXXXXXX.tar.gz \
#       ~/dragonradio-sense/m2 grid08:N210 grid01:N210 grid44:X310 grid53:X310
set -uo pipefail
tar=$(realpath "$1"); out=$(realpath -m "$2"); shift 2
specs=(); dur=60
while [ $# -gt 0 ]; do
    if [ "$1" = -- ]; then dur=$2; shift 2; else specs+=("$1"); shift; fi
done
[ ${#specs[@]} -gt 0 ] || { echo "no nodes" >&2; exit 2; }
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
cleanup() { [ -n "$ID" ] && { log "stopping reservation $ID"; gc reservation stop "$ID" >> "$out/campaign.log" 2>&1; }; }
trap cleanup EXIT

[ -r "$tar" ] || { log "no tree tarball $tar"; exit 2; }
gc reservations | grep -q '"oisac-' && { log "OpenISAC reservation active: not starting"; exit 3; }
name="dr-sense-m2-$(date +%H%M%S)"
log "scheduling $name on $nodes (1h, no flash)"
gc reservation schedule "$IMAGE" $nodes --name "$name" --duration 1h --no-flash \
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

log "sweep on ${ok[*]}, $dur s per cell"
pids=()
for spec in "${ok[@]}"; do
    n=${spec%%:*}; fam=${spec##*:}
    nssh "$n" "bash $WS/dragonradio/grid/run-sense-delay-m2.sh $WS/dragonradio '$out/$n' $fam $dur" \
        > "$out/run-$n.log" 2>&1 &
    pids+=($!)
done
status=0
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || { status=1; log "${ok[$i]}: sweep reported failures"; }
done
log "sweep done, status $status"
exit $status
