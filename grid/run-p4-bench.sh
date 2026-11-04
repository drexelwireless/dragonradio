#!/bin/bash
# P4 PHY benchmarks through the production PHY classes (CPU only).
# Usage: run-p4-bench.sh WORKSPACE OUTDIR [core] [packets]
# Builds bench/phy_profile.cc and bench/stream_throughput.cc against the
# workspace objects (built by build-fullstack.sh), then runs the stage
# profile and cold-construction timings pinned to one core, and the
# streaming-throughput matrix. See docs/performance.rst.
set -euo pipefail
ws=$(realpath "$1"); out=$(realpath -m "$2"); core=${3:-20}; packets=${4:-1000}
FS_CPPFLAGS=$(sed -n 's/^FS_CPPFLAGS = //p' "$ws/fullstack.mk")
FS_LIBS=$(sed -n 's/^FS_LIBS = //p' "$ws/fullstack.mk")
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial"
cd "$ws/dragonradio"
mkdir -p "$out" obj/bench
objects=$(find obj -name '*.o' ! -path 'obj/main.o' ! -path 'obj/tests/*' ! -path 'obj/bench/*' | sort)
for b in phy_profile stream_throughput; do
  [ -e bench/$b.cc ] || continue
  g++ -O2 -g -Wall -std=c++17 $FS_CPPFLAGS bench/$b.cc $objects -rdynamic $FS_LIBS -o obj/bench/$b
done
{
  echo "commit $(git rev-parse HEAD 2>/dev/null || cat COMMIT 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo ' (dirty)')"
  echo "host $(hostname); $(lscpu | sed -n 's/^Model name: *//p'); $(getconf _NPROCESSORS_ONLN) cpus"
  echo "governor $(cat /sys/devices/system/cpu/cpu$core/cpufreq/scaling_governor 2>/dev/null || echo unknown); core $core (siblings $(cat /sys/devices/system/cpu/cpu$core/topology/thread_siblings_list))"
  echo "compiler $(g++ --version | head -1); flags -O2 -g (portable, no -march)"
  echo "kernel $(uname -r); perf_event_paranoid $(cat /proc/sys/kernel/perf_event_paranoid); perf $(command -v perf || echo absent)"
  echo "load $(cat /proc/loadavg)"
  echo "packets $packets"
  echo "started $(date -Is)"
} > "$out/run-info.txt"
for i in 1 2 3 4 5; do
  taskset -c "$core" obj/bench/phy_profile cold native
  taskset -c "$core" obj/bench/phy_profile cold liquid
done > "$out/cold.jsonl" 2> "$out/cold.log"
taskset -c "$core" obj/bench/phy_profile profile "$out/profile.json" "$packets" 1000 2048 2> >(grep -v '^PHY:' > "$out/profile.log")
if [ -x obj/bench/stream_throughput ]; then
  obj/bench/stream_throughput "$out/stream.json" "$core" 2> >(grep -v '^PHY:' > "$out/stream.log")
fi
echo "finished $(date -Is) load $(cat /proc/loadavg)" >> "$out/run-info.txt"
