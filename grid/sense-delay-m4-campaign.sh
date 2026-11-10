#!/bin/bash
# Step M4 of docs/sensing-delay-measurement.rst (on codex/ofdma-simulation):
# end-to-end reaction test on pairs of grid nodes, run from gridgpu01.
# TRANSMITS on two nodes at once (grid/run-sense-delay-m4-cell.sh): 128-sample
# tone bursts at 1.322 GHz, at most 5 MHz, TX gain from a short link check
# (0, then 10, then 20 dB). Run only with Kapil's go-ahead; the standing
# approvals cover 1.322 GHz, TX gain up to 20 dB and 5 MHz, and no
# transmission while an OpenISAC (oisac-*) reservation is active.
#
# Reserves every node with the class image (no flash, no DragonRadio service,
# no traffic), checks each radio without transmitting, installs a built tree,
# then runs the pairs ONE AT A TIME. For each pair:
#   1. Link check at 1 MS/s, 15 s per gain: the lowest TX gain at which A
#      gets a reply to at least 95% of its pings is used for the pair.
#   2. Both directions (A pings and B reacts, then the reverse), each at 1 and
#      5 MS/s, with B at three settings:
#        best     rx_max_samps_factor 1, timed reply 0.2 ms ahead
#        untimed  rx_max_samps_factor 1, untimed reply
#        default  rx_max_samps_factor 8, timed reply 5 ms ahead (the radio's
#                 defaults)
#      The pinger stays at factor 8 and a 5 ms lead, as in M3. Each cell pings
#      for 60 s; the period is set so B's holdoff never hides a ping.
# A watcher checks for an OpenISAC reservation every 30 s and creates
# OUT/STOP if one appears; no cell starts after that. The reservation is
# released whatever happens. Results: OUT/PAIR/{NODE}/NAME.{json,log}.
#
# Usage: sense-delay-m4-campaign.sh TREE_TAR OUT A:B:FAMILY...
#   TREE_TAR  tar.gz of a tree built at /tmp/drsense/dragonradio by
#             grid/build-in-class-image.sh (paths in its venv are absolute)
#   A:B:FAMILY  e.g. grid08:grid01:N210 grid44:grid53:X310
# DRY=1 prints what would run, reserving and transmitting nothing.
# Example:
#   sense-delay-m4-campaign.sh ~/dragonradio-port/ota-ws/ws-sense-XXXXXXXX.tar.gz \
#       ~/dragonradio-sense/m4 grid08:grid01:N210 grid44:grid53:X310
set -uo pipefail
tar=$(realpath "$1"); out=$(realpath -m "$2"); shift 2
pairs=("$@")
[ ${#pairs[@]} -gt 0 ] || { echo "no pairs" >&2; exit 2; }
DRY=${DRY:-0}
HEAD=dwslgrid.ece.drexel.edu; IMAGE=krd26/dragonradio-class-gr310-20261027-v2; KEY=~/.ssh/id_ed25519
WS=/tmp/drsense
CELL=$WS/dragonradio/grid/run-sense-delay-m4-cell.sh
GAINS="0 10 20"
PING_SECS=60; CHECK_SECS=15; REACT_HEAD=12; REACT_TAIL=10
mkdir -p "$out"
log() { echo "$(date -u '+%F %T') $*" | tee -a "$out/campaign.log"; }
gc() {
    if [ "$DRY" = 1 ]; then
        case "$1" in reservations) echo '9999 "dr-sense-m4-dry" RUNNING' ;; reservation) echo "dry RUNNING" ;; esac
        return 0
    fi
    ssh -n -o BatchMode=yes -o ConnectTimeout=60 "$HEAD" gridcli -q "$@"
}
ID=""
nssh() {
    local n=$1; shift
    if [ "$DRY" = 1 ]; then echo "[$n] $*" >> "$out/dry-commands.txt"; return 0; fi
    ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i $KEY -o GSSAPIAuthentication=no \
        -o ConnectTimeout=60 -o ServerAliveInterval=30 -o LogLevel=ERROR \
        -o UserKnownHostsFile="$out/known_hosts.$ID" -o StrictHostKeyChecking=accept-new \
        -o ProxyCommand="ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i $KEY -o LogLevel=ERROR -o ConnectTimeout=60 -W %h:%p $HEAD" \
        "$n" "$@" </dev/null
}
nap() { [ "$DRY" = 1 ] || sleep "$1"; }
WATCH=""
cleanup() {
    [ -n "$WATCH" ] && kill "$WATCH" 2>/dev/null
    [ -n "$ID" ] && [ "$DRY" != 1 ] && { log "stopping reservation $ID"; gc reservation stop "$ID" >> "$out/campaign.log" 2>&1; }
}
trap cleanup EXIT

# Ping period for a react setting: B ignores edges for its holdoff after each
# reply (the probe's default: lead + 3 chunks + burst + 5 ms, at least
# 25 ms), and the period must be at least twice that, and at least 60 ms.
spp() { case "$1" in N210) echo 363 ;; X310) echo 1996 ;; esac; }
period_for() {  # FAMILY RATE FACTOR LEAD
    awk -v s="$(spp "$1")" -v r="$2" -v f="$3" -v l="$4" 'BEGIN {
        h = l + 3*s*f/r + 128/r + 0.005; if (h < 0.025) h = 0.025
        p = 2.2*h; if (p < 0.06) p = 0.06
        printf "%.2f\n", int(p*100 + 0.999)/100 }'
}

# One cell: B reacts, A pings. Prints A's JSON summary path.
cell() {  # PAIRDIR FAMILY A B NAME RATE FACTOR LEAD TXG PINGSECS
    local d=$1 fam=$2 a=$3 b=$4 name=$5 rate=$6 f=$7 lead=$8 txg=$9 secs=${10}
    local period; period=$(period_for "$fam" "$rate" "$f" "$lead")
    local rdur=$((secs + REACT_HEAD + REACT_TAIL))
    echo "$(date -u '+%F %T') $name A=$a B=$b rate $rate factor $f lead $lead period $period gain $txg" >> "$d/cells.txt"
    nssh "$b" "bash $CELL $WS/dragonradio '$d/$b' $fam react $name.react $rate $f $lead $period $rdur $txg" \
        >> "$d/ssh-$b.log" 2>&1 &
    local bp=$!
    nap "$REACT_HEAD"
    nssh "$a" "bash $CELL $WS/dragonradio '$d/$a' $fam ping $name.ping $rate 8 0.005 $period $secs $txg" \
        >> "$d/ssh-$a.log" 2>&1
    local ra=$?
    wait "$bp"; local rb=$?
    echo "$d/$a/$name.ping.json"
    [ $ra -eq 0 ] && [ $rb -eq 0 ]
}

# Fraction of A's pings that got a reply, from A's summary.
reply_frac() {
    [ "$DRY" = 1 ] && { echo 1.0; return; }
    python3 -c 'import json,sys
s = json.load(open(sys.argv[1]))
p = s.get("pings") or 0
print(round((p - s.get("own_missed", 0) - s.get("reply_missed", 0))/p, 3) if p else 0)' "$1" 2>/dev/null || echo 0
}

[ -r "$tar" ] || { log "no tree tarball $tar"; exit 2; }
for p in "${pairs[@]}"; do
    IFS=: read -r a b fam <<< "$p"
    { [ -n "$a" ] && [ -n "$b" ] && { [ "$fam" = N210 ] || [ "$fam" = X310 ]; }; } || { echo "bad pair $p" >&2; exit 2; }
done
gc reservations | grep -q '"oisac-' && { log "OpenISAC reservation active: not starting"; exit 3; }
rm -f "$out/STOP" "$out/DONE"
nodes=$(for p in "${pairs[@]}"; do IFS=: read -r a b _ <<< "$p"; echo -n "$a $b "; done)
name="dr-sense-m4-$(date +%H%M%S)"
[ "$DRY" = 1 ] && name=dr-sense-m4-dry
log "scheduling $name on $nodes(2h, no flash)"
gc reservation schedule "$IMAGE" $nodes --name "$name" --duration 2h --no-flash \
    --no-dragonradio-service --no-traffic --poll-interval 15 >> "$out/campaign.log" 2>&1
nap 5
ID=$(gc reservations | sed -n "s/^\([0-9][0-9]*\) \"$name\".*/\1/p" | head -1)
[ -n "$ID" ] || { log "reservation not found"; exit 1; }
log "reservation $ID"
s=""
for i in $(seq 1 40); do s=$(gc reservation info "$ID" 2>&1 | tail -1 | awk '{print $NF}'); [ "$s" = RUNNING ] && break; nap 30; done
[ "$s" = RUNNING ] || { log "reservation state $s"; exit 1; }
nap 45

# Reach each node, stop the image's radio service, and check the radio
# without transmitting. An X310 must report FPGA compatibility 19.
declare -A ready
for p in "${pairs[@]}"; do
    IFS=: read -r a b fam <<< "$p"
    case "$fam" in N210) addr=addr=192.168.10.2 ;; X310) addr=addr=192.168.40.2 ;; esac
    for n in $a $b; do
        for t in 1 2 3 4 5 6; do nssh "$n" true && break; nap 20; done
        nssh "$n" "sudo -n systemctl disable --now dragonradio.service >/dev/null 2>&1; true"
        if [ "$DRY" = 1 ]; then probe=""; else
            probe=$(nssh "$n" "timeout 120 uhd_usrp_probe --args=$addr 2>&1" | tee "$out/probe-$n.txt" | grep -m1 -E "Expected FPGA compatibility|RuntimeError|Error:")
        fi
        if [ -n "$probe" ]; then log "$n ($fam): radio not usable: $probe"; continue; fi
        if ! nssh "$n" "rm -rf $WS && mkdir -p $WS && tar -xzf '$tar' -C $WS && test -x $WS/dragonradio/build/dragonradio && test -x $CELL"; then
            log "$n: install failed"; continue
        fi
        log "$n ($fam): ready, tree $([ "$DRY" = 1 ] && echo dry || nssh "$n" "cat $WS/dragonradio/COMMIT")"
        ready[$n]=1
    done
done

# OpenISAC watcher: no transmission while an oisac-* reservation is active.
if [ "$DRY" != 1 ]; then
    ( while [ ! -e "$out/DONE" ]; do
          if gc reservations | grep -q '"oisac-'; then touch "$out/STOP"; echo "$(date -u '+%F %T') OpenISAC reservation seen: STOP" >> "$out/campaign.log"; fi
          sleep 30
      done ) &
    WATCH=$!
fi
stopped() { [ -e "$out/STOP" ]; }

status=0
for p in "${pairs[@]}"; do
    IFS=: read -r a b fam <<< "$p"
    if [ -z "${ready[$a]:-}" ] || [ -z "${ready[$b]:-}" ]; then log "pair $a-$b: a node is not ready, skipped"; status=1; continue; fi
    stopped && { log "STOP: not starting pair $a-$b"; status=3; break; }
    d="$out/$a-$b"; mkdir -p "$d"
    log "pair $a-$b ($fam): link check at TX gain $GAINS dB"
    txg=""
    for g in $GAINS; do
        stopped && break
        j=$(cell "$d" "$fam" "$a" "$b" "check-g$g" 1e6 1 0.0002 "$g" "$CHECK_SECS")
        fr=$(reply_frac "$j")
        log "pair $a-$b: TX gain $g dB, replies to $fr of pings"
        if awk -v x="$fr" 'BEGIN { exit !(x >= 0.95) }'; then txg=$g; break; fi
    done
    stopped && { log "STOP during link check of $a-$b"; status=3; break; }
    [ -n "$txg" ] || { log "pair $a-$b: no reliable link at 20 dB, skipped"; status=1; continue; }
    log "pair $a-$b: sweep at TX gain $txg dB"
    for dir in "$a:$b" "$b:$a"; do
        pa=${dir%%:*}; pb=${dir##*:}
        for rate in 1e6 5e6; do
            for setting in best:1:0.0002 untimed:1:0 default:8:0.005; do
                IFS=: read -r sname f lead <<< "$setting"
                stopped && break 3
                cname="$pa-to-$pb-$fam-r$rate-$sname"
                if cell "$d" "$fam" "$pa" "$pb" "$cname" "$rate" "$f" "$lead" "$txg" "$PING_SECS" > /dev/null; then
                    log "$cname: ok, replies to $(reply_frac "$d/$pa/$cname.ping.json") of pings"
                else
                    log "$cname: a side failed"; status=1
                fi
            done
        done
    done
    stopped && { log "STOP: pair $a-$b cut short"; status=3; break; }
    log "pair $a-$b: done"
done
touch "$out/DONE"
log "campaign done, status $status"
exit $status
