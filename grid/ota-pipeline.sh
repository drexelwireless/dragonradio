#!/usr/bin/env bash
# Full-stack OTA campaign on the Drexel Grid, unattended, from the grid
# controller (gridgpu01). See docs/ota-fullstack-grid.rst.
#
#   grid-job start --cwd /tmp/drfs-ws/dragonradio --name dr-ota -- \
#       bash grid/ota-pipeline.sh ~/dragonradio-ota/run-YYYYMMDD CAMPAIGN.json
#
# Stages leave RUN/stage-N.done; rerunning the same command resumes after the
# last finished stage. On any failure the trap stops the reservation this run
# created (it also has the campaign's time limit).
#
#   0 preflight  no RF   campaign validates; Grid up; nodes READY; workspace tarball matches
#   1 launch     no RF   one reservation for the campaign's nodes (all cells), --no-flash, no services
#   2 install    no RF   extract the prebuilt workspace on every node (node-local /tmp)
#   3 health     no RF   NTP, privilege, tun, radio ping, read-only uhd_usrp_probe (FPGA compat),
#                        external reference lock and PPS edges on nodes that use them
#   4 sim-gate   no RF   clean-arq in simulation on a node, native and liquid: the exact build
#   5 trials     RF      grid/ota-campaign.py run --execute: paired native/liquid trials
#   6 stop       no RF   stop the reservation
#
# DRY_RUN=1 stops after stage 0. STOP_BEFORE=N stops before stage N (e.g. 5:
# everything up to RF, then stop the reservation).
#
# Image-only rehearsal: a campaign whose "workspace_tar" is empty installs
# nothing; stage 3 checks the image's own UHD, DragonRadio and reference
# inputs, stage 4 is skipped, and stage 5 is refused (no radio to run).
set -uo pipefail

RUN_ARG=${1:?usage: ota-pipeline.sh RUN_DIR CAMPAIGN.json}
CAMPAIGN_ARG=${2:?usage: ota-pipeline.sh RUN_DIR CAMPAIGN.json}
mkdir -p "$RUN_ARG"
RUN=$(cd "$RUN_ARG" && pwd -P)
HERE=$(cd "$(dirname "$0")" && pwd)
[ -e "$RUN/campaign.json" ] || cp "$CAMPAIGN_ARG" "$RUN/campaign.json"
cmp -s "$CAMPAIGN_ARG" "$RUN/campaign.json" || { echo "RUN/campaign.json differs from $CAMPAIGN_ARG; use a new RUN_DIR" >&2; exit 2; }
CAMPAIGN="$RUN/campaign.json"
PY=${PY:-$(cd "$HERE/../.." && pwd)/venv/bin/python}     # the controller workspace's Python (h5py)

cfg() { "$PY" -c 'import json, sys; c = json.load(open(sys.argv[1])); v = eval(sys.argv[2], {}, {"c": c}); print(" ".join(map(str, v)) if isinstance(v, list) else v)' "$CAMPAIGN" "$1"; }
HEAD=$(cfg 'c["ssh"]["head"]')
KEY=$(cfg 'c["ssh"]["key"]'); KEY=${KEY/#\~/$HOME}
IMAGE=$(cfg 'c["image"]')
DURATION=$(cfg 'c.get("reservation_duration", "4h")')
WS=$(cfg 'c["workspace"]')
WS_TAR=$(cfg 'c["workspace_tar"] or ""')
IMAGE_ONLY=0; [ -z "$WS_TAR" ] && IMAGE_ONLY=1
COMMIT=$(cfg 'c["workspace_commit"]')
# Reserved nodes: the union over the campaign's cells (host, radio address, clock source).
HOSTS=$("$PY" "$HERE/ota-campaign.py" hosts "$CAMPAIGN") || { echo "campaign nodes do not validate" >&2; exit 2; }
NODES=$(awk '{print $1}' <<< "$HOSTS" | xargs)
ADDRS=$(awk '{print $2}' <<< "$HOSTS" | xargs)
REFS=$(awk '{print $3}' <<< "$HOSTS" | xargs)
GATE_NODES=$(awk '!seen[$4]++ {print $1}' <<< "$HOSTS" | xargs)   # first node of each radio family
SSH_COMMON=(-F /dev/null -x -n -o BatchMode=yes -o IdentitiesOnly=yes -i "$KEY" -o GSSAPIAuthentication=no
            -o ConnectTimeout=60 -o ServerAliveInterval=30 -o LogLevel=ERROR)

log() { echo "$(date '+%F %T') $*" | tee -a "$RUN/pipeline.log"; }
die() { log "ERROR: $*"; exit 1; }
head_ssh() { ssh "${SSH_COMMON[@]}" -o StrictHostKeyChecking=yes "$HEAD" "$@"; }
gridcli() { head_ssh gridcli -q "$@"; }
node_ssh() {
    # Container host keys change with every launch: pinned per reservation
    # (first contact, in stage 2, accepts them; ota-fullstack.py then requires them).
    local node=$1; shift
    timeout "${SSH_TIMEOUT:-900}" ssh "${SSH_COMMON[@]}" -o UserKnownHostsFile="$RUN/known_hosts.$(cat "$RUN/reservation.id")" \
        -o StrictHostKeyChecking=accept-new \
        -o ProxyCommand="ssh -F /dev/null -x -o BatchMode=yes -o IdentitiesOnly=yes -i $KEY -o LogLevel=ERROR -o ConnectTimeout=60 -W %h:%p $HEAD" \
        "$node" "$@"
}
stage() {
    local n=$1 name=$2 fn=$3
    if [ -e "$RUN/stage-$n.done" ]; then log "stage $n ($name): already done"; return 0; fi
    if [ -n "${STOP_BEFORE:-}" ] && [ "$n" -ge "$STOP_BEFORE" ] && [ "$n" -lt 6 ]; then
        log "STOP_BEFORE=$STOP_BEFORE: skipping stage $n ($name)"; return 0
    fi
    log "stage $n ($name): start"
    "$fn" || die "stage $n ($name) failed"
    touch "$RUN/stage-$n.done"
    log "stage $n ($name): done"
}

cleanup() {
    local rc=$?
    if [ -s "$RUN/reservation.id" ] && [ ! -e "$RUN/stage-6.done" ]; then
        log "cleanup: stopping reservation $(cat "$RUN/reservation.id") (exit status $rc)"
        if gridcli reservation stop "$(cat "$RUN/reservation.id")" >> "$RUN/pipeline.log" 2>&1; then
            mv "$RUN/reservation.id" "$RUN/reservation.id.stopped-$(date +%s)"
            rm -f "$RUN"/stage-[1-4].done
        else
            log "cleanup: STOP FAILED; stop reservation $(cat "$RUN/reservation.id") by hand"
        fi
    fi
}
trap cleanup EXIT

s0_preflight() {
    "$PY" "$HERE/ota-campaign.py" plan "$CAMPAIGN" --run-dir "$RUN" >> "$RUN/pipeline.log" 2>&1 \
        || die "campaign does not validate (see pipeline.log)"
    if [ "$IMAGE_ONLY" = 1 ]; then
        log "preflight: image-only rehearsal (no workspace): nothing is installed, no trial can run"
    else
        [ -s "$WS_TAR" ] || die "workspace tarball $WS_TAR missing"
        local tar_commit
        tar_commit=$(tar -xzOf "$WS_TAR" "$(basename "$WS")/dragonradio/COMMIT" 2>/dev/null)
        [ "$tar_commit" = "$COMMIT" ] || die "workspace tarball commit $tar_commit, campaign says $COMMIT"
    fi
    gridcli status | grep -q "'disabled': False" || die "Grid is disabled or unreachable"
    head_ssh getent hosts "${DNS_CANARY:-gridgpu01}" > /dev/null || die "name resolution on $HEAD is broken"
    local ready n
    ready=$(gridcli nodes)
    for n in $NODES; do
        grep -q "^$n READY" <<< "$ready" || die "$n is not READY ($(grep "^$n " <<< "$ready"))"
    done
    log "preflight: nodes $NODES; image $IMAGE; commit $COMMIT"
    if [ "${DRY_RUN:-0}" = 1 ]; then log "DRY_RUN=1: stopping before launch"; touch "$RUN/dry-run.ok"; exit 0; fi
}

s1_launch() {
    if [ ! -s "$RUN/reservation.id" ]; then
        local name; name="dr-ota-$(basename "$RUN")-$(date +%H%M%S)"
        # shellcheck disable=SC2086
        gridcli reservation schedule "$IMAGE" $NODES --name "$name" --duration "$DURATION" \
            --no-flash --no-dragonradio-service --no-traffic --async >> "$RUN/pipeline.log" 2>&1 \
            || die "reservation request failed"
        sleep 5
        gridcli reservations | sed -n "s/^\([0-9][0-9]*\) \"$name\".*/\1/p" | head -1 > "$RUN/reservation.id"
        [ -s "$RUN/reservation.id" ] || die "reservation $name not found"
    fi
    local id info state t0; id=$(cat "$RUN/reservation.id"); t0=$(date +%s)
    while :; do
        info=$(gridcli reservation info "$id" 2>&1 | tail -1); state=$(awk '{print $NF}' <<< "$info")
        log "reservation $id: $info"
        case $state in
            RUNNING) return 0 ;;
            *FAIL*|STOPPED|ABORTED|*INVALID*) die "reservation $id ended in $state" ;;
        esac
        [ $(( $(date +%s) - t0 )) -gt 2400 ] && die "reservation $id not RUNNING after 40 min"
        sleep 60
    done
}

s2_install() {
    local n tries
    for n in $NODES; do
        tries=0
        until head_ssh getent hosts "$n" > /dev/null || [ $tries -ge 10 ]; do tries=$((tries + 1)); sleep 30; done
        if [ "$IMAGE_ONLY" = 1 ]; then
            # First contact pins the host key; record what the image carries.
            node_ssh "$n" "hostname; cat /usr/local/share/sdr-class-image/* 2>/dev/null; nproc; grep -m1 'model name' /proc/cpuinfo" \
                > "$RUN/install-$n.log" 2>&1 || die "first contact with $n failed (install-$n.log)"
            log "install: $n reachable (image only, nothing installed)"
            continue
        fi
        node_ssh "$n" "rm -rf '$WS' && mkdir -p '$(dirname "$WS")' && tar -xzf '$WS_TAR' -C '$(dirname "$WS")' && cat '$WS/dragonradio/COMMIT'" \
            > "$RUN/install-$n.log" 2>&1 || die "install on $n failed (install-$n.log)"
        grep -qx "$COMMIT" "$RUN/install-$n.log" || die "install on $n: wrong commit"
        log "install: $n ok"
    done
}

s3_health() {
    # Every node is checked (a full picture for the rehearsal), then any failure fails the stage.
    # The check runs from RUN (shared home), so it needs no workspace on the node.
    cp "$HERE/ota-node-health.sh" "$RUN/ota-node-health.sh"
    local n addr ref i=0 addrs refs bad=""
    read -r -a addrs <<< "$ADDRS"
    read -r -a refs <<< "$REFS"
    for n in $NODES; do
        addr=${addrs[$i]}; ref=${refs[$i]}; i=$((i + 1))
        if node_ssh "$n" "IMAGE_ONLY=$IMAGE_ONLY bash '$RUN/ota-node-health.sh' '$WS' '$addr' '$COMMIT' '$ref'" > "$RUN/health-$n.log" 2>&1; then
            log "health: $n ok ($(grep -m1 -o 'Mboard: .*' "$RUN/health-$n.log"); reference $ref)"
        else
            log "health: $n FAILED: $(grep FAIL "$RUN/health-$n.log" | tr '\n' ' ')"
            bad="$bad $n"
        fi
    done
    [ -z "$bad" ] || die "health failed on:$bad (health-NODE.log)"
}

s4_gate() {
    # The exact build, on a radio node's CPU (the first node of each radio
    # family, whose containers may run on different hosts), in simulation:
    # native and the liquid production configuration.
    local n try phy
    if [ "$IMAGE_ONLY" = 1 ]; then log "sim gate: skipped (image-only rehearsal, no workspace)"; return 0; fi
    for n in $GATE_NODES; do
        for phy in native liquid; do
            local args=""; [ $phy = liquid ] && args="-- --phy ofdm -c rs8"
            for try in 1 2; do
                local g="gate-$n-$phy-$try"
                if node_ssh "$n" "bash '$WS/dragonradio/grid/run-sim-fullstack.sh' '$WS' '$RUN/$g' --scenario clean-arq $args" \
                    > "$RUN/$g.log" 2>&1 && grep -q "PASSED" "$RUN/$g.log" && ! grep -q "FAILED" "$RUN/$g.log"; then
                    log "sim gate: $phy clean-arq passed on $n (try $try)"; break
                fi
                log "sim gate: $phy clean-arq failed on $n (try $try; $g.log)"
                [ $try = 2 ] && return 1
            done
        done
    done
}

s5_trials() {
    [ "$IMAGE_ONLY" = 1 ] && { log "trials refused: image-only campaign (no workspace)"; return 1; }
    "$PY" "$HERE/ota-campaign.py" run "$CAMPAIGN" --run-dir "$RUN" --reservation "$(cat "$RUN/reservation.id")" \
        --python "$PY" --execute 2>&1 | tee -a "$RUN/pipeline.log"
    return "${PIPESTATUS[0]}"
}

s6_stop() {
    gridcli reservation stop "$(cat "$RUN/reservation.id")" >> "$RUN/pipeline.log" 2>&1 || die "reservation stop failed"
}

log "pipeline start: run $RUN, campaign $CAMPAIGN"
stage 0 preflight s0_preflight
stage 1 launch s1_launch
stage 2 install s2_install
stage 3 health s3_health
stage 4 sim-gate s4_gate
stage 5 trials s5_trials
stage 6 stop s6_stop
log "pipeline done"
