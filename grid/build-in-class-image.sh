#!/bin/bash
# Build this DragonRadio tree on a node running the GNU Radio 3.10 class image
# (krd26/dragonradio-class-gr310-20261027-v2: Ubuntu 26.04, GCC 15, Python 3.14,
# UHD 3.9.5 cc15d9ce), the same way the image builds its own DragonRadio
# (sdr-lab-class image/gr3.10/build-in-container.sh, stage dragonradio). The
# image's third-party libraries (liquid-dsp, libcorrect, firpm under /usr/local)
# and its extern/ sources are reused; nothing under /usr/local is changed.
#
# Usage: build-in-class-image.sh SRC_DIR [JOBS]
#   SRC_DIR  this tree (e.g. extracted from git archive); built in SRC_DIR/build
# Then run the radio with:
#   VIRTUAL_ENV=/usr/local/dragonradio/venv PYTHONPATH=SRC_DIR/python/dragonradio/src SRC_DIR/build/dragonradio SCRIPT
set -euo pipefail
src=$(realpath "$1"); jobs=${2:-$(nproc)}
image=/usr/local/dragonradio
[ -d "$image/extern" ] || { echo "not the class image: no $image/extern" >&2; exit 2; }
cd "$src"
# Header-only and source dependencies from the image's own tree.
for d in "$image"/extern/*; do
    name=$(basename "$d")
    if [ ! -e "extern/$name" ] || [ -z "$(ls -A "extern/$name" 2>/dev/null)" ]; then
        rm -rf "extern/$name"; ln -s "$d" "extern/$name"
    fi
done
# As the image: the system pybind11 (3.0) instead of the submodule.
sed -i 's|^add_subdirectory(extern/pybind11)|find_package(pybind11 CONFIG REQUIRED)|' CMakeLists.txt
export SETUPTOOLS_SCM_PRETEND_VERSION=0+$(cat COMMIT 2>/dev/null | cut -c1-7 || echo port)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_FLAGS="-include cstdint -include cstdio" -DPython3_EXECUTABLE="$image/venv/bin/python"
make -C build -j "$jobs"
ln -sfn build/dragonradio dragonradio
echo "built $(cat COMMIT 2>/dev/null || echo '?') in $src/build"
