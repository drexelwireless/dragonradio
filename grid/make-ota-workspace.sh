#!/bin/bash
# Build the workspace that grid/ota-pipeline.sh installs on every radio node,
# on the grid controller (gridgpu01, Ubuntu 24.04 x86_64, like the
# dragonradio/ubuntu2404 radio images). No radio is opened.
# Usage: make-ota-workspace.sh DEV_WORKSPACE OUT_DIR
#   DEV_WORKSPACE  a setup-fullstack-build.sh workspace whose dragonradio/ is a git
#                  checkout at the commit to deploy (clean working tree)
#   OUT_DIR        shared directory for the bundle and tarball (NFS home)
# Steps: package the commit (scripts/package-fullstack.py), set it up offline at
# /tmp/drfs-ws from the bundle alone (builds and runs every tests/fullstack
# program), run the clean-arq scenario there (native and liquid), then tar
# /tmp/drfs-ws to OUT_DIR/ws-COMMIT.tar.gz. The radio objects are built
# without -march=native (grid/build-fullstack.sh), so they run on any x86_64
# node with the same Ubuntu release; the pipeline's simulation gate checks
# that on a radio node before anything transmits.
set -euo pipefail
dev=$(realpath "$1"); out=$(mkdir -p "$2" && realpath "$2")
ws=/tmp/drfs-ws
cd "$dev/dragonradio"
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "working tree not clean" >&2; exit 2; }
commit=$(git rev-parse HEAD)
short=${commit:0:8}
log() { echo "$(date '+%F %T') $*" | tee -a "$out/make-ws-$short.log"; }
log "commit $commit"
bundle_dir="$out/bundle-$short"
rm -rf "$bundle_dir"
"$dev/venv/bin/python" scripts/package-fullstack.py --workspace "$dev" --output-dir "$bundle_dir" >> "$out/make-ws-$short.log" 2>&1
bundle=$(find "$bundle_dir" -maxdepth 1 -type d -name 'dragonradio-fullstack-*' | head -1)
[ -n "$bundle" ] || { log "no bundle produced"; exit 1; }
log "bundle $bundle"
[ ! -e "$ws" ] || { log "$ws exists; move it aside first"; exit 2; }
bash "$bundle/setup.sh" "$ws" > "$out/setup-$short.log" 2>&1 || { log "setup failed: $out/setup-$short.log"; exit 1; }
log "setup and tests passed: $(grep -c '^PASSED' "$out/setup-$short.log") test programs"
[ "$(cat "$ws/dragonradio/COMMIT")" = "$commit" ] || { log "workspace COMMIT mismatch"; exit 1; }
for phy in native liquid; do
    args=(); [ $phy = liquid ] && args=(-- --phy ofdm -c rs8)
    bash "$ws/dragonradio/grid/run-sim-fullstack.sh" "$ws" "$out/sim-$short-$phy" --scenario clean-arq "${args[@]}" \
        > "$out/sim-$short-$phy.log" 2>&1 || true
    grep -q PASSED "$out/sim-$short-$phy.log" && ! grep -q FAILED "$out/sim-$short-$phy.log" \
        && log "clean-arq $phy: passed" || log "clean-arq $phy: FAILED (see sim-$short-$phy.log)"
done
tar -czf "$out/ws-$short.tar.gz" -C /tmp --exclude='drfs-ws/packages' --exclude='drfs-ws/build-*' --exclude='drfs-ws/liquid-src' --exclude='drfs-ws/src-*' drfs-ws
log "workspace tarball $out/ws-$short.tar.gz ($(du -h "$out/ws-$short.tar.gz" | cut -f1)); commit $commit"
