#!/bin/bash
# Paired before/after comparison of two builds of the P4 PHY benchmarks
# (bench/phy_profile.cc, bench/stream_throughput.cc), alternating the two in
# randomized order within each block on the same pinned cores.
# Usage: run-p4-ab.sh WORKSPACE A_BIN_DIR B_BIN_DIR OUTDIR [blocks] [core] [packets]
# Each *_BIN_DIR holds phy_profile and stream_throughput built against that
# revision's objects (run-p4-bench.sh builds them into obj/bench).
set -euo pipefail
ws=$(realpath "$1"); a=$(realpath "$2"); b=$(realpath "$3"); out=$(realpath -m "$4")
blocks=${5:-2}; core=${6:-20}; packets=${7:-1000}
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial"
mkdir -p "$out"
{
  echo "A $a"; echo "B $b"; echo "blocks $blocks core $core packets $packets"
  echo "host $(hostname); governor $(cat /sys/devices/system/cpu/cpu$core/cpufreq/scaling_governor 2>/dev/null)"
  echo "started $(date -Is) load $(cat /proc/loadavg)"
} > "$out/run-info.txt"
for ((k = 1; k <= blocks; k++)); do
  order=$( (echo A; echo B) | shuf)
  echo "block $k order $(echo $order)" >> "$out/run-info.txt"
  for v in $order; do
    dir=$a; [ "$v" = B ] && dir=$b
    taskset -c "$core" "$dir/phy_profile" profile "$out/profile-$v-$k.json" "$packets" 1000 2048 2> >(grep -v '^PHY:' > "$out/profile-$v-$k.log")
    "$dir/stream_throughput" "$out/stream-$v-$k.json" "$core" 2> >(grep -v '^PHY:' > "$out/stream-$v-$k.log")
  done
done
echo "finished $(date -Is) load $(cat /proc/loadavg)" >> "$out/run-info.txt"
