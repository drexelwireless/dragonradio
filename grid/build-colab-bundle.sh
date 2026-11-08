#!/bin/bash
# Build a self-contained DragonRadio simulation bundle for Google Colab.
#
# Run as root inside an Ubuntu 22.04 (jammy) root, the base system Colab uses,
# with the build packages installed (see grid/colab-chroot.sh) and a
# standalone CPython 3.12 (python-build-standalone, install_only) at
# /opt/python. Usage:
#   build-colab-bundle.sh SOURCE.tar.gz LIQUID.tar.gz OUTDIR
#     SOURCE.tar.gz  git archive of the tree (prefix dragonradio/)
#     LIQUID.tar.gz  pinned liquidDSP with generated configure (as setup-fullstack-build.sh)
#
# The bundle carries its own Python, so it does not depend on the Python
# version of the Colab runtime. Only glibc and libstdc++ come from the host,
# which is why it is built on jammy (glibc 2.35). Layout:
#   drsim/bin/dragonradio        rpath $ORIGIN/../lib:$ORIGIN/../python/lib
#   drsim/lib/                   every shared library ldd finds outside glibc
#   drsim/python/                CPython 3.12 with numpy, scipy, h5py, ...
#   drsim/dragonradio/           python/dragonradio, scripts/, config/ from the tree
#   drsim/env.sh                 sets PATH, PYTHONPATH and LD_LIBRARY_PATH
set -euo pipefail
src=$(realpath "$1"); liquid=$(realpath "$2"); out=$(realpath -m "$3")
XSIMD_REV=3d17850eee16c2556a0cd7083b96be3aa8090f45
FIRPM_REV=6b41bce2b8ea913da891c44af2f21af259b2632a
LIBCORRECT_REV=2c8db6d8d764dc9f72f4b085fb0b7bf9b3c09457
PYBIND11_VERSION=2.13.6
PY=/opt/python/bin/python3
PREFIX=/opt/drdeps
JOBS=$(getconf _NPROCESSORS_ONLN)
work=/build/work
rm -rf "$work" "$PREFIX"; mkdir -p "$work" "$PREFIX" "$out"
cd "$work"
log() { printf '%s %s\n' "$(date -Is)" "$*"; }

log "python packages"
$PY -m pip install -q --upgrade pip
$PY -m pip install -q "numpy>=1.24,<3" "pybind11==$PYBIND11_VERSION" "protobuf>=4,<6" \
  ipython libconf pandas psutil python-daemon pyzmq scipy h5py
PYBIND_INC=$($PY -c 'import pybind11; print(pybind11.get_include())')

log "xsimd, firpm, libcorrect"
git clone -q https://github.com/QuantStack/xsimd.git xsimd && git -C xsimd checkout -q $XSIMD_REV
git clone -q https://github.com/sfilip/firpm.git firpm && git -C firpm checkout -q $FIRPM_REV
cmake -S firpm/firpm_d -B build-firpm -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX \
  -DBUILD_TESTING=OFF > firpm.log 2>&1
cmake --build build-firpm -j "$JOBS" >> firpm.log 2>&1 && cmake --install build-firpm >> firpm.log 2>&1
git clone -q https://github.com/drexelwireless/libcorrect.git libcorrect && git -C libcorrect checkout -q $LIBCORRECT_REV
cmake -S libcorrect -B build-libcorrect -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX \
  -DCMAKE_C_FLAGS=-O3 > libcorrect.log 2>&1
cmake --build build-libcorrect -j "$JOBS" >> libcorrect.log 2>&1
cmake --build build-libcorrect --target shim -j "$JOBS" >> libcorrect.log 2>&1
cmake --install build-libcorrect >> libcorrect.log 2>&1
[ -e $PREFIX/include/fec.h ] || { echo "libfec shim not installed" >&2; exit 1; }

log "liquidDSP"
mkdir liquid && tar --no-same-owner -xzf "$liquid" -C liquid --strip-components=1
# Some archives carry configure but not config.h.in; regenerate both.
[ -e liquid/config.h.in ] || ( cd liquid && ./bootstrap.sh > ../liquid-bootstrap.log 2>&1 )
( cd liquid && CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib -Wl,-rpath,$PREFIX/lib" \
  CFLAGS="-O3 -std=gnu99" ./configure --prefix=$PREFIX > ../liquid.log 2>&1 \
  && make -j "$JOBS" >> ../liquid.log 2>&1 && make install >> ../liquid.log 2>&1 )
grep -q "#define HAVE_FEC_H 1" liquid/config.h || { echo "liquid configured without libfec" >&2; exit 1; }

log "dragonradio"
tar --no-same-owner -xzf "$src"
cd dragonradio
version_py=python/dragonradio/dragonradio/radio/version.py
[ -e "$version_py" ] || echo "version = 'colab-bundle'" > "$version_py"
A=x86_64-linux-gnu
CPPFLAGS="-DDRAGONRADIO_NATIVE_FEC -Isrc -I$PREFIX/include -I/usr/include/hdf5/serial \
-I/opt/python/include/python3.12 -I$PYBIND_INC -I$work/xsimd/include"
LIBS="-L$PREFIX/lib -L/usr/lib/$A/hdf5/serial -L/opt/python/lib -lc -lfftw3f -lliquid -lm -lpthread -luhd \
-lhdf5_hl_cpp -lhdf5_cpp -lhdf5_hl -lhdf5 -lz -ldl -lpython3.12 -lFLAC++ -lFLAC -lfftw3 -lfirpm -lcap -lfec \
-Wl,-rpath,\$\$ORIGIN/../lib:\$\$ORIGIN/../python/lib"
# Portable optimization: Colab hosts vary, so no -march=native.
make -j "$JOBS" CXX=g++ LINKER=g++ CPPFLAGS="$CPPFLAGS" CXXFLAGS="-O2 -g -Wall -std=c++17" \
  LIBS="$LIBS" dragonradio > ../dragonradio.log 2>&1 || { tail -40 ../dragonradio.log; exit 1; }

log "bundle"
b="$work/drsim"; rm -rf "$b"; mkdir -p "$b/bin" "$b/lib" "$b/dragonradio"
strip -o "$b/bin/dragonradio" dragonradio
cp -a /opt/python "$b/python"
cp -a python scripts config tests "$b/dragonradio/" 2>/dev/null || true
# Every shared library outside glibc (and libstdc++/libgcc_s, which Colab has
# at the same or a newer version), resolved with the build's search path.
export LD_LIBRARY_PATH="$PREFIX/lib:/opt/python/lib"
ldd dragonradio | awk '/=> \//{print $3}' | while read -r lib; do
  case "$(basename "$lib")" in
    libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|ld-linux*|libstdc++.so.*|libgcc_s.so.*|libpython3.12.so*) ;;
    *) cp -L "$lib" "$b/lib/" ;;
  esac
done
for f in "$b"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$f"; done
cat > "$b/env.sh" <<'EOF'
# Source this to use the bundle: . drsim/env.sh
DRSIM=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export DRSIM
export PATH="$DRSIM/bin:$DRSIM/python/bin:$PATH"
export PYTHONHOME="$DRSIM/python"
export PYTHONPATH="$DRSIM/dragonradio/python/dragonradio"
export LD_LIBRARY_PATH="$DRSIM/lib:$DRSIM/python/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
EOF
{
  echo "built $(date -Is) on $(. /etc/os-release; echo "$PRETTY_NAME"), glibc $(ldd --version | head -1 | awk '{print $NF}')"
  echo "commit $(cat COMMIT 2>/dev/null || echo unknown)"
  echo "compiler $(g++ --version | head -1)"
  echo "python $($PY -c 'import sys; print(sys.version.split()[0])')"
  echo "libraries:"; ls "$b/lib" | sed 's/^/  /'
  $PY -m pip freeze | sed 's/^/  pip /'
} > "$b/MANIFEST.txt"
tar -C "$work" -czf "$out/drsim.tar.gz" drsim
log "wrote $out/drsim.tar.gz ($(du -h "$out/drsim.tar.gz" | cut -f1))"
