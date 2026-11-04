#!/bin/bash
# Private, no-sudo build workspace for the full DragonRadio stack on Ubuntu 24.04.
# Usage: setup-fullstack-build.sh SOURCE.tar.gz LIQUID.tar.gz WORKSPACE
#   SOURCE.tar.gz  archive of the tree under test (prefix dragonradio/)
#   LIQUID.tar.gz  pinned liquidDSP checkout with generated configure/config.h.in
#   WORKSPACE      new directory (container-local disk recommended)
# Development packages are downloaded with `apt download` and unpacked into
# WORKSPACE/deps; nothing is installed system-wide. This is a MODERNIZED build
# (UHD 4.6, Boost 1.83, Python 3.12, current pybind11), not the historical
# Ubuntu 20.04 / UHD 3.9 / Python 3.8 image. Versions are recorded in
# WORKSPACE/build-manifest.txt.
# With VENDOR=DIR (a bundle from scripts/package-fullstack.py), nothing is
# fetched: Ubuntu packages come from DIR/debs, Python packages from
# DIR/wheels, and xsimd/firpm/libcorrect from DIR/*.tar.gz at the same pins.
set -euo pipefail
umask 022
source_bundle=$(realpath "$1"); liquid_bundle=$(realpath "$2"); ws=$3
XSIMD_REV=3d17850eee16c2556a0cd7083b96be3aa8090f45     # dependencies/xsimd submodule pin
FIRPM_REV=6b41bce2b8ea913da891c44af2f21af259b2632a     # dependencies/firpm submodule pin
LIBCORRECT_REV=2c8db6d8d764dc9f72f4b085fb0b7bf9b3c09457 # dependencies/libcorrect submodule pin
PYBIND11_VERSION=2.13.6                                 # pinned 787d2c8 predates Python 3.11+
LIQUID_REV=d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb    # dependencies/liquid-dsp submodule pin
VENDOR=${VENDOR:-}
[ -z "$VENDOR" ] || VENDOR=$(realpath "$VENDOR")
[ -e "$ws" ] && { echo "workspace exists: $ws" >&2; exit 2; }
mkdir -p "$ws"; ws=$(realpath "$ws"); cd "$ws"
log() { printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$ws/setup.log"; }
manifest() { printf '%s\n' "$*" >> "$ws/build-manifest.txt"; }
PKGS="libuhd-dev libboost-dev libhdf5-dev libflac++-dev libcap-dev libpython3.12-dev python3.12-dev libfftw3-dev libeigen3-dev protobuf-compiler"
# Runtime libraries the recursion misses (time_t-64 renames / virtual names);
# found by ldd on the linked executable.
RUNTIME="libboost-chrono1.83.0t64 libboost-filesystem1.83.0 libboost-serialization1.83.0 libboost-thread1.83.0 libboost-atomic1.83.0 libboost-program-options1.83.0 libboost-date-time1.83.0 libboost-system1.83.0 libsz2 libogg0 libaec0"

mkdir -p packages deps
if [ -n "$VENDOR" ] && [ -d "$VENDOR/debs" ]; then
  log "vendored packages ($(ls "$VENDOR"/debs/*.deb | wc -l))"
  cp "$VENDOR"/debs/*.deb packages/
else
log "resolving dependencies"
apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances $PKGS 2>/dev/null \
  | grep '^[a-z0-9]' | sort -u > packages/candidates.txt
: > packages/needed.txt
while read -r p; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || echo "$p" >> packages/needed.txt
done < packages/candidates.txt
for p in $RUNTIME; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || echo "$p" >> packages/needed.txt
done
sort -u -o packages/needed.txt packages/needed.txt
log "downloading $(wc -l < packages/needed.txt) packages"
( cd packages && while read -r p; do apt download "$p" >/dev/null 2>&1 || echo "skip $p (virtual or unavailable)" >> ../setup.log; done < needed.txt )
fi
for deb in packages/*.deb; do dpkg-deb -x "$deb" deps; manifest "deb $(basename "$deb")"; done

P="$ws/deps/usr"
ARCH=x86_64-linux-gnu
mkdir -p prefix/include prefix/lib
export PATH="$P/bin:$PATH"
export LD_LIBRARY_PATH="$ws/prefix/lib:$P/lib/$ARCH:$P/lib/$ARCH/hdf5/serial${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Unpacked -dev packages carry absolute symlinks (e.g. libfoo.so -> /usr/lib/...);
# repoint them inside deps so the linker finds the unpacked targets.
find deps -type l | while read -r link; do
  target=$(readlink "$link")
  case "$target" in /*) [ -e "deps$target" ] && ln -sfn "$ws/deps$target" "$link" ;; esac
done
# Runtime libraries already installed on the host were not downloaded; point
# dangling development links (libfoo.so -> libfoo.so.N) at the host copies.
find deps/usr/lib/$ARCH -maxdepth 1 -type l | while read -r link; do
  [ -e "$link" ] && continue
  host="/usr/lib/$ARCH/$(basename "$(readlink "$link")")"
  [ -e "$host" ] && ln -sfn "$host" "$link"
done

log "python virtualenv"
python3 -m venv venv
if [ -n "$VENDOR" ] && [ -d "$VENDOR/wheels" ]; then
  # Exact versions of the packaging workspace, from bundled wheels.
  venv/bin/python -m pip install -q --no-index --find-links "$VENDOR/wheels" -r "$VENDOR/wheels/requirements.txt"
else
venv/bin/python -m pip install -q "numpy>=1.24,<3" "cmake>=3.27,<4" "pybind11==$PYBIND11_VERSION" "protobuf>=4,<6"
# dragonradio package dependencies; setup.py pins (numpy 1.21, scipy 1.7, ...)
# predate Python 3.12, so current releases are used and recorded. netifaces
# needs a C build against system Python headers and is omitted.
venv/bin/python -m pip install -q ipython libconf pandas psutil python-daemon pyzmq scipy
fi
venv/bin/python -m pip freeze | sed 's/^/pip /' >> "$ws/build-manifest.txt"
manifest "pybind11 $PYBIND11_VERSION (pip)"
PYBIND_INC=$(venv/bin/python -c 'import pybind11; print(pybind11.get_include())')

log "xsimd $XSIMD_REV and firpm $FIRPM_REV"
# Vendored source archives or a clone at the pin; the manifest records the pin.
fetch() {  # NAME URL REV
  if [ -n "$VENDOR" ] && [ -e "$VENDOR/$1.tar.gz" ]; then
    mkdir "src-$1" && tar -xzf "$VENDOR/$1.tar.gz" -C "src-$1" --strip-components=1
    manifest "$1 $3 (vendored)"
  else
    git clone -q "$2" "src-$1" && git -C "src-$1" checkout -q "$3"
    manifest "$1 $(git -C "src-$1" rev-parse HEAD)"
  fi
}
fetch xsimd https://github.com/QuantStack/xsimd.git "$XSIMD_REV"
fetch firpm https://github.com/sfilip/firpm.git "$FIRPM_REV"
fdir=src-firpm/firpm_d    # DragonRadio's FIRDesign.cc uses firpm's double-precision API
venv/bin/cmake -S "$fdir" -B build-firpm -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ws/prefix" \
  -DCMAKE_PREFIX_PATH="$P" -DCMAKE_CXX_FLAGS="-I$P/include" \
  -DBUILD_TESTING=OFF > build-firpm.log 2>&1
venv/bin/cmake --build build-firpm -j 16 >> build-firpm.log 2>&1
venv/bin/cmake --install build-firpm >> build-firpm.log 2>&1

# libcorrect and its libfec-compatible shim (libfec + fec.h), as build.sh
# installs them before liquid-dsp: liquid's convolutional and Reed-Solomon
# codes (v27/v29/punctured, rs8) exist only when it finds libfec.
log "libcorrect $LIBCORRECT_REV with libfec shim"
fetch libcorrect https://github.com/drexelwireless/libcorrect.git "$LIBCORRECT_REV"
venv/bin/cmake -S src-libcorrect -B build-libcorrect -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ws/prefix" \
  -DCMAKE_C_FLAGS="-O3" > build-libcorrect.log 2>&1
venv/bin/cmake --build build-libcorrect -j 16 >> build-libcorrect.log 2>&1
venv/bin/cmake --build build-libcorrect --target shim -j 16 >> build-libcorrect.log 2>&1
venv/bin/cmake --install build-libcorrect >> build-libcorrect.log 2>&1
[ -e "$ws/prefix/include/fec.h" ] && [ -e "$ws/prefix/lib/libfec.so" ] || { echo "libfec shim not installed" >&2; exit 1; }

log "pinned liquidDSP"
mkdir liquid-src
tar -xzf "$liquid_bundle" -C liquid-src --strip-components=1
if [ -e liquid-src/.git ]; then manifest "liquid $(git -C liquid-src rev-parse HEAD)"; else manifest "liquid $LIQUID_REV (archive)"; fi
( cd liquid-src && CPPFLAGS="-I$ws/prefix/include -I$P/include" LDFLAGS="-L$ws/prefix/lib -L$P/lib/$ARCH -Wl,-rpath,$ws/prefix/lib" CFLAGS="-O3 -std=gnu99" ./configure --prefix="$ws/prefix" > ../build-liquid.log 2>&1 \
  && make -j 16 >> ../build-liquid.log 2>&1 && make install >> ../build-liquid.log 2>&1 )
grep -q "#define HAVE_FEC_H 1" liquid-src/config.h || { echo "liquid configured without libfec" >&2; exit 1; }
manifest "liquid FEC: libfec shim (HAVE_FEC_H)"

log "source tree"
tar -xzf "$source_bundle"
cat > "$ws/fullstack.mk" <<EOF
# Generated by setup-fullstack-build.sh. Include and library flags for building
# the production sources in this workspace (see grid/build-fullstack.sh).
FS_CPPFLAGS = -DDRAGONRADIO_NATIVE_FEC -Isrc -I$ws/prefix/include -I$P/include -I$P/include/$ARCH -I$P/include/hdf5/serial -I$P/include/python3.12 -I$P/include/$ARCH/python3.12 -I$PYBIND_INC -I$ws/src-xsimd/include
FS_LIBS = -L$ws/prefix/lib -L$P/lib/$ARCH -L$P/lib/$ARCH/hdf5/serial -Wl,-rpath,$ws/prefix/lib -Wl,-rpath,$P/lib/$ARCH -Wl,-rpath,$P/lib/$ARCH/hdf5/serial -Wl,-rpath-link,$P/lib/$ARCH -lc -lfftw3f -lliquid -lm -lpthread -luhd -lhdf5_hl_cpp -lhdf5_cpp -lhdf5_hl -lhdf5 -lz -ldl -lpython3.12 -lFLAC++ -lFLAC -lfftw3 -lfirpm -lcap -lfec
EOF
manifest "uhd $(ls "$P"/lib/$ARCH | grep -o 'libuhd.so.[0-9.]*' | head -1)"
manifest "compiler $(g++ --version | head -1)"
log "workspace ready: $ws"
