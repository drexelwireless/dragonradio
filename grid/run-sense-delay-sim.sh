#!/bin/bash
# Step M1 of docs/sensing-delay-measurement.rst: check the sensing-delay probe
# (scripts/sense-delay.py) in simulation, where the receive latency is a known
# setting. Run in a workspace after build-fullstack.sh.
# Usage: run-sense-delay-sim.sh WORKSPACE OUTDIR [DURATION]
set -euo pipefail
ws=$(realpath "$1"); out=$(realpath -m "$2"); dur=${3:-20}
export PATH="$ws/deps/usr/bin:$PATH"
export PYTHONPATH="$ws/dragonradio/python/dragonradio:$ws/venv/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$ws/prefix/lib:$ws/deps/usr/lib/x86_64-linux-gnu:$ws/deps/usr/lib/x86_64-linux-gnu/hdf5/serial${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$ws/dragonradio"
mkdir -p "$out"
dr="$ws/dragonradio/dragonradio"
probe="scripts/sense-delay.py"
{
  echo "host $(hostname) $(getconf _NPROCESSORS_ONLN) cpus"
  echo "started $(date -Is)"
} > "$out/run-info.txt"

relay_pid=
start_relay() {
  sock="$out/air-$1.sock"
  "$ws/venv/bin/python" scripts/sim-air.py "$sock" --stats "$out/$1.air.json" > "$out/$1.air.log" 2>&1 &
  relay_pid=$!
  for _ in $(seq 50); do [ -S "$sock" ] && return; sleep 0.1; done
  echo "relay did not start" >&2; exit 1
}
stop_relay() { kill -INT "$relay_pid" 2>/dev/null || true; wait "$relay_pid" 2>/dev/null || true; }
trap 'stop_relay' EXIT

# Receive only: arrival delay should be the simulated latency plus nothing
# much, whatever the chunk size.
for lat in 0.002 0.010; do
  for f in 8 1; do
    name="rx-lat${lat}-f$f"
    start_relay "$name"
    "$dr" "$probe" --sim-air "$sock" --node-id 1 --mode rx --duration "$dur" \
      --sim-rx-latency "$lat" --rx-max-samps-factor "$f" --out "$out/$name.json" > "$out/$name.log" 2>&1
    stop_relay
  done
done

# React and ping pair: node 1 reacts, node 2 pings and hears itself through
# TX leakage. The end-to-end delay measured by node 2 should match node 1's
# planned delay (edge to requested TX time).
pair() {
  local name=$1 lat=$2 lead=$3 f=$4
  start_relay "$name"
  "$dr" "$probe" --sim-air "$sock" --node-id 1 --mode react --duration "$((dur + 3))" \
    --sim-rx-latency "$lat" --lead "$lead" --rx-max-samps-factor "$f" \
    --out "$out/$name.react.json" > "$out/$name.react.log" 2>&1 &
  local rpid=$!
  sleep 1
  "$dr" "$probe" --sim-air "$sock" --node-id 2 --mode ping --duration "$dur" --period 0.1 \
    --sim-rx-latency "$lat" --lead 0.020 --rx-max-samps-factor "$f" --sim-tx-leakage-db -10 \
    --out "$out/$name.ping.json" > "$out/$name.ping.log" 2>&1
  wait "$rpid"
  stop_relay
}
pair pair-lat0.002-lead5ms-f8 0.002 0.005 8
pair pair-lat0.010-lead5ms-f8 0.010 0.005 8
pair pair-lat0.002-lead5ms-f1 0.002 0.005 1
pair pair-lat0.002-untimed-f1 0.002 0 1

echo "finished $(date -Is)" >> "$out/run-info.txt"
