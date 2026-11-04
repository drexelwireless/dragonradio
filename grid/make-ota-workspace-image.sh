#!/bin/bash
# Build the OTA workspace that grid/ota-pipeline.sh installs on every radio
# node, for this branch on the GNU Radio 3.10 class image
# (krd26/dragonradio-class-gr310-20261027-v2). Run on the grid controller
# (gridgpu01) with a node of an existing reservation running the class image;
# the build and the checks run on that node. No radio is opened: the checks
# are the fullstack test programs and the clean-arq scenario over the
# simulated device.
#
# Usage: make-ota-workspace-image.sh [--ssh CMD] NODE OUT_DIR
#   --ssh CMD  command that runs a command on NODE, as "CMD NODE COMMAND"
#              (default ssh; on the grid the nodes are reached through the
#              login server, e.g. --ssh ~/dragonradio-port/nodessh.sh)
#   NODE       a class-image node (it is not reserved or changed beyond
#              /tmp/drport)
#   OUT_DIR    shared directory (NFS home) for the tarball and logs
#
# Steps, all on NODE: ship the committed HEAD of this clone (git archive; the
# working tree must be clean) to a fresh /tmp/drport/dragonradio; build it
# with grid/build-in-class-image.sh (build/ and venv/ inside the tree); build
# and run every tests/fullstack program; run clean-arq in simulation, native
# and liquid (grid/run-sim-fullstack.sh); then tar /tmp/drport/dragonradio,
# with build/dragonradio, the test programs and venv/ but without object
# files, to OUT_DIR/ws-SHORT.tar.gz. ota-pipeline.sh extracts it at the same
# path (campaign "workspace": "/tmp/drport"), where the venv's absolute
# paths hold, and checks OUT_DIR's commit against the campaign's.
set -euo pipefail
ssh_cmd=ssh
if [ "${1:-}" = --ssh ]; then ssh_cmd=$2; shift 2; fi
node=${1:?usage: make-ota-workspace-image.sh [--ssh CMD] NODE OUT_DIR}
out=$(mkdir -p "${2:?usage: make-ota-workspace-image.sh [--ssh CMD] NODE OUT_DIR}" && realpath "$2")
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "working tree not clean" >&2; exit 2; }
commit=$(git rev-parse HEAD)
short=${commit:0:8}
log() { echo "$(date '+%F %T') $*" | tee -a "$out/make-ws-$short.log"; }
on_node() { $ssh_cmd "$node" "$@"; }
ws=/tmp/drport
tree=$ws/dragonradio
log "commit $commit on $node"
# Ship the commit (stdin) to a fresh tree.
git archive --prefix=dragonradio/ HEAD | on_node "set -e; mkdir -p $ws; rm -rf $tree $ws/new; mkdir -p $ws/new; tar -x -C $ws/new; echo $commit > $ws/new/dragonradio/COMMIT; mv $ws/new/dragonradio $tree; rmdir $ws/new"
# Build and test.
if on_node "bash $tree/grid/build-in-class-image.sh $tree \$(nproc) && make -C $tree/build -j\$(nproc) fullstack-tests" \
        > "$out/build-$short.log" 2>&1; then
    log "build ok ($out/build-$short.log)"
else
    log "build FAILED ($out/build-$short.log)"; exit 1
fi
if on_node "cd $tree/build/tests/fullstack && fail=0; for t in test_*; do [ -x \$t ] || continue; timeout 900 ./\$t > /dev/null 2>&1 && echo \"PASSED \$t\" || { echo \"FAILED \$t\"; fail=1; }; done; exit \$fail" \
        > "$out/tests-$short.log" 2>&1; then
    log "fullstack tests passed: $(grep -c '^PASSED' "$out/tests-$short.log") programs"
else
    log "fullstack tests FAILED: $(grep '^FAILED' "$out/tests-$short.log" | tr '\n' ' ')"; exit 1
fi
# The simulation gate, as the pipeline's stage 4 runs it.
gate=ok
for phy in native liquid; do
    args=""; [ $phy = liquid ] && args="-- --phy ofdm -c rs8"
    on_node "bash $tree/grid/run-sim-fullstack.sh $ws $out/sim-$short-$phy --scenario clean-arq $args" \
        > "$out/sim-$short-$phy.log" 2>&1 || true
    if grep -q PASSED "$out/sim-$short-$phy.log" && ! grep -q FAILED "$out/sim-$short-$phy.log"; then
        log "clean-arq $phy: passed"
    else
        log "clean-arq $phy: FAILED (see sim-$short-$phy.log)"; gate=failed
    fi
done
[ $gate = ok ] || exit 1
# The workspace: the tree with its build products and venv, not the objects.
on_node "tar -czf $out/ws-$short.tar.gz -C /tmp --exclude='drport/dragonradio/build/CMakeFiles' \
    --exclude='drport/dragonradio/build/src' --exclude='*.o' --exclude='*.a' --exclude='__pycache__' drport/dragonradio"
[ "$(tar -xzOf "$out/ws-$short.tar.gz" drport/dragonradio/COMMIT)" = "$commit" ] || { log "tarball COMMIT mismatch"; exit 1; }
log "workspace tarball $out/ws-$short.tar.gz ($(du -h "$out/ws-$short.tar.gz" | cut -f1)); commit $commit; campaign \"workspace\": \"$ws\""
