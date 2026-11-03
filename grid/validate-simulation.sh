#!/bin/sh
# Run on a grid host without opening the attached SDR.
set -eu
cd "$(dirname "$0")/.."
task_python=${PYTHON:-python3}
"$task_python" scripts/build-phy.py --fftw-prefix "${FFTW_PREFIX:-/usr}"
make -f Makefile.simulation sim-test PYTHON="$task_python"
"$task_python" scripts/frame-radio.py --mode ofdm --engine native --frames 100
"$task_python" scripts/frame-radio.py --mode ofdma --engine native --frames 100
