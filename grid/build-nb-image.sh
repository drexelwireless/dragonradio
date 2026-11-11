#!/bin/bash
# Install DragonRadio (this branch) and the notebook layer at a fixed prefix in
# a container running the class image, ready to snapshot as dragonradio-nb.
# Run as root on the node. Uses grid/build-in-class-image.sh and
# grid/build-notebook-layer.sh unchanged; nothing under /usr/local is changed
# except the /usr/local/bin/drnb link.
#
# Usage: sudo build-nb-image.sh TREE.tar.gz [PREFIX] [JOBS]
#   TREE.tar.gz  git archive of this branch with a COMMIT file (prefix dragonradio/)
#   PREFIX       default /opt/drnb
# Result:
#   PREFIX/dragonradio    the tree, built in build/, with venv/
#   PREFIX/nbvenv         JupyterLab layer and the dragonradio / drnb-python3 kernels
#   PREFIX/etc/jupyter    server defaults (kernels allowed, default kernel)
#   PREFIX/bin/drnb       the launcher (linked from /usr/local/bin/drnb)
#   PREFIX/notebooks      starter notebooks (.ipynb from notebooks/start/*.py)
#   PREFIX/MANIFEST.txt   commit, base image, versions
#   /var/lib/drnb/slots   one lock file per launcher slot
set -euo pipefail
tarball=$(realpath "$1"); prefix=${2:-/opt/drnb}; jobs=${3:-$(nproc)}
[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 2; }
[ -d /usr/local/dragonradio/extern ] || { echo "not the class image" >&2; exit 2; }
log() { printf '%s %s\n' "$(date -Is)" "$*"; }
umask 022

log "tree"
rm -rf "$prefix"; mkdir -p "$prefix"
tar --no-same-owner -xzf "$tarball" -C "$prefix"
tree=$prefix/dragonradio
mkdir -p "$tree/extern"

log "build (grid/build-in-class-image.sh) and full-stack tests"
bash "$tree/grid/build-in-class-image.sh" "$tree" "$jobs" > "$prefix/build.log" 2>&1
make -C "$tree/build" -j "$jobs" fullstack-tests >> "$prefix/build.log" 2>&1
(cd "$tree/build" && ctest -L fullstack --output-on-failure) > "$prefix/ctest.log" 2>&1 \
  || { tail -30 "$prefix/ctest.log"; exit 1; }
tail -3 "$prefix/ctest.log"

log "notebook layer (grid/build-notebook-layer.sh)"
bash "$tree/grid/build-notebook-layer.sh" "$tree" "$prefix/nbvenv" "$jobs" > "$prefix/layer.log" 2>&1 \
  || { tail -30 "$prefix/layer.log"; exit 1; }
tail -4 "$prefix/layer.log"

log "jupyter defaults, launcher, slots"
mkdir -p "$prefix/etc/jupyter" "$prefix/bin"
cat > "$prefix/etc/jupyter/jupyter_server_config.py" <<'EOF'
# dragonradio-nb image defaults (grid/build-nb-image.sh). Only the image's two
# kernels are offered; the image's /usr/bin/python3 kernel does not see the
# notebook layer.
c.KernelSpecManager.allowed_kernelspecs = {"drnb-python3", "dragonradio"}
c.MultiKernelManager.default_kernel_name = "drnb-python3"
c.ServerApp.ip = "127.0.0.1"
c.ServerApp.open_browser = False
EOF
install -m 0755 "$tree/grid/drnb" "$prefix/bin/drnb"
# Starter notebooks (jupytext .py sources -> .ipynb), copied to each user's
# ~/drnb-notebooks by drnb on first use.
mkdir -p "$prefix/notebooks"
for py in "$tree"/notebooks/start/*.py; do
  "$prefix/nbvenv/bin/jupytext" --quiet --to ipynb -o "$prefix/notebooks/$(basename "${py%.py}").ipynb" "$py"
done
ls "$prefix/notebooks"
ln -sfn "$prefix/bin/drnb" /usr/local/bin/drnb
mkdir -p /var/lib/drnb/slots
for i in $(seq 0 63); do : > "/var/lib/drnb/slots/$i"; done
chmod 0644 /var/lib/drnb/slots/*

log "manifest"
{
  echo "dragonradio-nb image contents, built $(date -Is) on $(hostname)"
  echo "commit $(cat "$tree/COMMIT" 2>/dev/null || echo unknown)"
  echo "base image dragonradio-class-gr310-20261027-v2 ($(. /etc/os-release; echo "$PRETTY_NAME"))"
  echo "image DragonRadio /usr/local/dragonradio (unchanged)"
  echo "python $("$prefix/nbvenv/bin/python" --version 2>&1)"
  echo "kernels $("$prefix/nbvenv/bin/python" -c 'from jupyter_client.kernelspec import KernelSpecManager as K; print(sorted(K().find_kernel_specs()))')"
  echo "packages (notebook layer):"
  "$prefix/nbvenv/bin/python" -m pip freeze --local 2>/dev/null | sed 's/^/  /'
} > "$prefix/MANIFEST.txt"
head -6 "$prefix/MANIFEST.txt"
log "done: $prefix"
