#!/bin/bash
# Coded and uncoded PER over AWGN through the production PHY classes (P3).
# Usage: run-fec-benchmark.sh WORKSPACE OUTDIR [packets-per-point] [payload-bytes] [config-substring]
# WORKSPACE is from setup-fullstack-build.sh, already built with build-fullstack.sh.
set -euo pipefail
ws=$(realpath "$1"); out=$2; packets=${3:-400}; payload=${4:-1000}; only=${5:-}
FS_CPPFLAGS=$(sed -n 's/^FS_CPPFLAGS = //p' "$ws/fullstack.mk")
FS_LIBS=$(sed -n 's/^FS_LIBS = //p' "$ws/fullstack.mk")
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial"
cd "$ws/dragonradio"
mkdir -p "$out" obj/bench
objects=$(find obj -name '*.o' ! -path 'obj/main.o' ! -path 'obj/tests/*' ! -path 'obj/bench/*' | sort)
g++ -O2 -g -Wall -std=c++17 $FS_CPPFLAGS bench/fec_per.cc $objects -rdynamic $FS_LIBS -o obj/bench/fec_per
{
  echo "commit $(cat COMMIT 2>/dev/null || echo unknown)"
  echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus; $(g++ --version | head -1)"
  echo "packets_per_point $packets payload_bytes $payload"
  echo "started $(date -Is)"
} > "$out/run-info.txt"
obj/bench/fec_per "$out/fec_per.csv" "$packets" "$payload" $only 2> "$out/fec_per.log"
echo "finished $(date -Is)" >> "$out/run-info.txt"
