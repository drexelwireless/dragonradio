#!/bin/bash
# Build the notebook layer on a node running the GNU Radio 3.10 class image
# (krd26/dragonradio-class-gr310-20261027-v2), above a tree already built by
# grid/build-in-class-image.sh. Nothing under /usr/local is changed.
#
#  * NBVENV (virtualenv): JupyterLab, ipympl, papermill, nbclient, jupytext,
#    nbstripout and myst-nb at the versions pinned in requirements-notebooks.txt.
#    Like build-in-class-image.sh's venv, it sees this tree's dragonradio
#    package (and dragonradio.tools) first, then the image venv's packages.
#  * NBVENV/dragonradio-build: this tree built again with
#    -DDRAGONRADIO_NOTEBOOK=ON (dragonradiocpp.notebook helpers). The tree's
#    own build/ stays the default build.
#  * NBVENV/bin/dragonradio-python: a symlink to that dragonradio executable.
#    Run under a name ending in "python", dragonradio is a plain Python
#    interpreter (src/main.cc) with dragonradiocpp built in; in NBVENV/bin it
#    uses NBVENV as its virtual environment.
#  * The "dragonradio" Jupyter kernel (NBVENV/share/jupyter/kernels), which
#    runs ipykernel on dragonradio-python, so a notebook can call the native
#    and liquid PHYs in-process; and "drnb-python3", NBVENV's own python
#    (no dragonradiocpp). The image's "python3" kernel runs /usr/bin/python3,
#    which does not see NBVENV.
#
# Idempotent: rerun after rebuilding the tree.
#
# Usage: build-notebook-layer.sh SRC_DIR [NBVENV] [JOBS]
#   SRC_DIR  tree built by grid/build-in-class-image.sh (e.g. /tmp/drnb/dragonradio)
#   NBVENV   default /tmp/drnb/nbvenv
# Then: NBVENV/bin/jupyter-lab --no-browser --ip=127.0.0.1
set -euo pipefail
src=$(realpath "$1"); nbvenv=${2:-/tmp/drnb/nbvenv}; jobs=${3:-$(nproc)}
image=/usr/local/dragonradio
[ -d "$image/venv" ] || { echo "not the class image: no $image/venv" >&2; exit 2; }
[ -x "$src/build/dragonradio" ] || { echo "not built: $src/build/dragonradio (run grid/build-in-class-image.sh first)" >&2; exit 2; }
case "$nbvenv" in /usr/local/*) echo "refusing to install under /usr/local" >&2; exit 2;; esac

py=$("$image/venv/bin/python" -c 'import sys; print(f"python{sys.version_info[0]}.{sys.version_info[1]}")')
[ -x "$nbvenv/bin/python" ] || virtualenv -q --system-site-packages -p python3 "$nbvenv"
site=$nbvenv/lib/$py/site-packages
# This tree's packages first, then the image venv's (as build-in-class-image.sh).
printf '%s\n' "$src/python/dragonradio/src" "$src/python/dragonradio-tools/src" > "$site/00-dragonradio-port.pth"
"$image/venv/bin/python" -c 'import site; print("\n".join(p for p in site.getsitepackages() if "/usr/local/dragonradio/venv" in p))' > "$site/50-image-venv.pth"

"$nbvenv/bin/python" -m pip install -q --progress-bar off --disable-pip-version-check -r "$src/requirements-notebooks.txt"

# The dragonradio executable with the notebook helpers, built out of tree so
# that grid/node-build.sh's rsync --delete leaves it alone.
nbbuild=$nbvenv/dragonradio-build
cmake -S "$src" -B "$nbbuild" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_FLAGS="-include cstdint -include cstdio" -DPython3_EXECUTABLE="$image/venv/bin/python" \
    -DDRAGONRADIO_NOTEBOOK=ON > "$nbvenv/cmake.log"
make -C "$nbbuild" -j "$jobs" dragonradio > "$nbvenv/make.log" 2>&1
ln -sfn "$nbbuild/dragonradio" "$nbvenv/bin/dragonradio-python"

kdir=$nbvenv/share/jupyter/kernels/dragonradio
mkdir -p "$kdir"
cat > "$kdir/kernel.json" <<EOF
{
  "argv": ["$nbvenv/bin/dragonradio-python", "-m", "ipykernel_launcher", "-f", "{connection_file}"],
  "display_name": "DragonRadio (in-process PHY)",
  "language": "python",
  "env": {"VIRTUAL_ENV": "$nbvenv", "DRAGONRADIO_TREE": "$src"},
  "metadata": {"debugger": false}
}
EOF
kdir=$nbvenv/share/jupyter/kernels/drnb-python3
mkdir -p "$kdir"
cat > "$kdir/kernel.json" <<EOF
{
  "argv": ["$nbvenv/bin/python", "-m", "ipykernel_launcher", "-f", "{connection_file}"],
  "display_name": "Python 3 (notebook layer)",
  "language": "python",
  "env": {"VIRTUAL_ENV": "$nbvenv", "DRAGONRADIO_TREE": "$src"},
  "metadata": {"debugger": true}
}
EOF

# Check: the layer's tools, and dragonradiocpp in the kernel's interpreter.
"$nbvenv/bin/python" -c 'import jupyterlab, ipympl, papermill, nbclient, jupytext, myst_nb; print("nbvenv: jupyterlab", jupyterlab.__version__, "ipympl", ipympl.__version__, "papermill", papermill.__version__)'
"$nbvenv/bin/dragonradio-python" -c 'import sys, dragonradiocpp.notebook, dragonradio.radio, ipykernel; print("kernel python:", sys.executable, "prefix", sys.prefix)' 2>&1 | grep -v -e '^linux; GNU C++' -e '^$' -e 'smallest subnormal' -e 'return self._float_to_str'
# jupyter_core and jupyter_client come from the image, so NBVENV/bin has no
# jupyter or jupyter-kernelspec; ask jupyter_client directly.
"$nbvenv/bin/python" -c 'from jupyter_client.kernelspec import KernelSpecManager as K; print("kernels:", K().find_kernel_specs())'
echo "notebook layer ready: $nbvenv/bin/jupyter-lab --no-browser --ip=127.0.0.1"
