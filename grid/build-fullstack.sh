#!/bin/bash
# Build the production DragonRadio sources in a workspace from setup-fullstack-build.sh.
# Usage: build-fullstack.sh WORKSPACE [make targets...]   (default target: dragonradio)
set -uo pipefail
ws=$(realpath "$1"); shift
targets=${*:-dragonradio}
FS_CPPFLAGS=$(sed -n 's/^FS_CPPFLAGS = //p' "$ws/fullstack.mk")
FS_LIBS=$(sed -n 's/^FS_LIBS = //p' "$ws/fullstack.mk")
export PATH="$ws/deps/usr/bin:$PATH"
cd "$ws/dragonradio"
# setuptools_scm normally writes this at install time; archives carry no git metadata.
version_py=python/dragonradio/dragonradio/radio/version.py
[ -e "$version_py" ] || echo "version = 'workspace-build'" > "$version_py"
# Pure-Python dependencies of the dragonradio package live in the workspace venv.
export PYTHONPATH="$ws/venv/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Portable optimization (no -march=native) so objects run on any grid node.
make -k -j "$(getconf _NPROCESSORS_ONLN)" CXX=g++ LINKER=g++ \
  CPPFLAGS="$FS_CPPFLAGS" CXXFLAGS="-O2 -g -Wall -std=c++17" LIBS="$FS_LIBS" $targets
status=$?
[ $status -ne 0 ] && exit $status
# Production-path tests: link each tests/fullstack/*.cc against every
# production object except main.o, then run it.
if [ -d tests/fullstack ]; then
  objects=$(find obj -name '*.o' ! -path 'obj/main.o' ! -path 'obj/tests/*' | sort)
  mkdir -p obj/tests
  for t in tests/fullstack/*.cc; do
    exe="obj/tests/$(basename "${t%.cc}")"
    g++ -O2 -g -Wall -std=c++17 $FS_CPPFLAGS "$t" $objects -rdynamic $FS_LIBS -o "$exe" || exit 1
    echo "== $exe"
    "$exe" || status=1
  done
  for t in tests/fullstack/*.py; do
    echo "== $t (embedded Python)"
    ./dragonradio "$t" || status=1
  done
fi
exit $status
