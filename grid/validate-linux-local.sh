#!/bin/bash
# CPU-only validation in a unique private directory; no sudo/SDR/system installs.
# Inputs: source bundle and standalone pinned liquid Git checkout archive.
# The dependency archive must include bootstrap-generated configure/config.h.in.
# Dependencies are unpacked into this private tree.
set -euo pipefail
umask 077
bundle=$(realpath "$1")
liquid_bundle=$(realpath "$2")
workspace=$(mktemp -d /tmp/dragonradio-linux-XXXXXX)
echo "$workspace"
cd "$workspace"
mkdir packages deps
cd packages
apt download libfftw3-dev libfftw3-single3
for package in ./*.deb; do dpkg-deb -x "$package" ../deps; done
cd ..
mkdir prefix
ln -s "$workspace/deps/usr/include" prefix/include
ln -s "$workspace/deps/usr/lib/x86_64-linux-gnu" prefix/lib
export LD_LIBRARY_PATH="$workspace/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 -m venv venv
venv/bin/python -m pip install numpy
export PYTHON="$workspace/venv/bin/python"
tar -xzf "$bundle"
cd dragonradio-frame
mkdir "$workspace/liquid-source"
tar -xzf "$liquid_bundle" -C "$workspace/liquid-source" --strip-components=1
"$PYTHON" scripts/build-modem.py --liquid-source "$workspace/liquid-source" --fftw-prefix "$workspace/prefix"
FFTW_PREFIX="$workspace/prefix" bash grid/validate-simulation.sh
"$PYTHON" scripts/ota-phy.py prepare --campaign "$workspace/native-campaign" --frames 30 > "$workspace/prepare.log" 2>&1 || {
  # Full campaign requires actual liquid bridge; do not silently relabel its absence.
  cat "$workspace/prepare.log"
  exit 1
}
"$PYTHON" scripts/ota-phy.py simulate --campaign "$workspace/native-campaign" > "$workspace/campaign.log"
