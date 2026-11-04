#!/bin/bash
# Correctness builds of the production sources (P4): AddressSanitizer with
# UndefinedBehaviorSanitizer, or ThreadSanitizer, in a separate object
# directory (obj-asan, obj-tsan). Builds dragonradio-KIND, links and runs
# every tests/fullstack/*.cc against the instrumented objects, runs the
# embedded-Python tests through dragonradio-KIND, then full-stack scenarios
# with the instrumented radio (scripts/sim-fullstack.py --dragonradio).
# Usage: build-sanitized.sh WORKSPACE asan|tsan OUTDIR [scenario ...]
# Not for performance measurements.
set -uo pipefail
run=""
ws=$(realpath "$1"); kind=$2; out=$(realpath -m "$3"); shift 3
scenarios=${*:-clean-arq erasure-arq saturation}
case $kind in
  asan) san="-fsanitize=address,undefined -fno-sanitize=vptr"
        export ASAN_OPTIONS="abort_on_error=0:detect_leaks=1:halt_on_error=0:detect_stack_use_after_return=1"
        export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"
        export LSAN_OPTIONS="suppressions=$ws/dragonradio/grid/lsan.supp" ;;
  # TSan's runtime aborts ("unexpected memory mapping") under the high ASLR
  # entropy of recent kernels; each TSan process runs with its own ASLR
  # disabled (setarch -R, no host setting changed).
  tsan) san="-fsanitize=thread"; run="setarch $(uname -m) -R"
        export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:history_size=4:suppressions=$ws/dragonradio/grid/tsan.supp" ;;
  *) echo "kind must be asan or tsan" >&2; exit 2 ;;
esac
FS_CPPFLAGS=$(sed -n 's/^FS_CPPFLAGS = //p' "$ws/fullstack.mk")
FS_LIBS=$(sed -n 's/^FS_LIBS = //p' "$ws/fullstack.mk")
export PATH="$ws/deps/usr/bin:$PATH"
export PYTHONPATH="$ws/dragonradio/python/dragonradio:$ws/venv/lib/python3.12/site-packages"
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial"
cd "$ws/dragonradio"
mkdir -p "$out"
version_py=python/dragonradio/dragonradio/radio/version.py
[ -e "$version_py" ] || echo "version = 'workspace-build'" > "$version_py"
flags="-O1 -g -fno-omit-frame-pointer -Wall -std=c++17 $san"
make -k -j "$(getconf _NPROCESSORS_ONLN)" -f Makefile -f grid/sanitize.mk SANITIZER=$kind OBJDIR=obj-$kind \
  CXX=g++ LINKER=g++ CPPFLAGS="$FS_CPPFLAGS" CXXFLAGS="$flags" LDFLAGS="-rdynamic $san" LIBS="$FS_LIBS" \
  dragonradio-$kind > "$out/build.log" 2>&1 || { echo "build failed; see $out/build.log"; exit 1; }
status=0
objects=$(find obj-$kind -name '*.o' ! -path "obj-$kind/main.o" ! -path "obj-$kind/tests/*" | sort)
mkdir -p obj-$kind/tests
for t in tests/fullstack/*.cc; do
  name=$(basename "${t%.cc}")
  g++ $flags $FS_CPPFLAGS "$t" $objects -rdynamic $san $FS_LIBS -o "obj-$kind/tests/$name" >> "$out/build.log" 2>&1 || { echo "== $name: link failed"; status=1; continue; }
  $run "obj-$kind/tests/$name" > "$out/$name.log" 2>&1
  rc=$?
  reports=$(grep -c -E "ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|WARNING: ThreadSanitizer" "$out/$name.log")
  echo "== $name: exit $rc, sanitizer reports $reports"
  [ $rc -ne 0 ] || [ "$reports" -ne 0 ] && status=1
done
# Python hosts leak by design at exit (interpreter state); leaks off there.
for t in tests/fullstack/*.py; do
  name=$(basename "${t%.py}")
  ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=0" $run ./dragonradio-$kind "$t" > "$out/$name.log" 2>&1
  rc=$?
  reports=$(grep -c -E "ERROR: AddressSanitizer|runtime error:|WARNING: ThreadSanitizer" "$out/$name.log")
  echo "== $name (embedded Python): exit $rc, sanitizer reports $reports"
  [ $rc -ne 0 ] || [ "$reports" -ne 0 ] && status=1
done
# Full stack: the instrumented radio as both nodes. Slower than real time
# allows at full rate, so failures of timing checks are expected and kept;
# the sanitizer reports are the result here.
"$ws/venv/bin/python" -c 'import h5py' 2>/dev/null || "$ws/venv/bin/python" -m pip install -q h5py
radio="$ws/dragonradio/dragonradio-$kind"
if [ -n "$run" ]; then
  radio="$out/dragonradio-$kind-wrapper"
  printf '#!/bin/sh\nexec %s %s "$@"\n' "$run" "$ws/dragonradio/dragonradio-$kind" > "$radio"
  chmod +x "$radio"
fi
for sc in $scenarios; do
  ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=0" "$ws/venv/bin/python" scripts/sim-fullstack.py \
    --dragonradio "$radio" --outdir "$out/sim" --scenario "$sc" --timeout 120 \
    > "$out/sim-$sc.log" 2>&1
  rc=$?
  reports=$(cat "$out/sim/$sc"/node*.log 2>/dev/null | grep -c -E "ERROR: AddressSanitizer|runtime error:|WARNING: ThreadSanitizer")
  echo "== scenario $sc: exit $rc ($(grep -E '^   (PASSED|FAILED)' "$out/sim-$sc.log" | tail -1 | tr -d ' ')), sanitizer reports $reports"
  [ "$reports" -ne 0 ] && status=1
done
exit $status
