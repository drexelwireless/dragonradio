#!/bin/bash
# Execute notebooks/colab/drsim_colab_probe.ipynb headless in the Ubuntu 22.04
# root used by build-colab-bundle.sh, pinned to a few logical CPUs to mimic a
# Colab runtime (the free tier is 2 vCPUs: one core's two hyperthreads).
# Usage (as root on the host): run-colab-probe.sh ROOT CPULIST OUTNAME
#   ROOT     the jammy root, with /build/out/drsim.tar.gz and /opt/nbenv
#   CPULIST  taskset list, e.g. 0,36 (siblings of one core) or 0-3
#   OUTNAME  name for the executed notebook and report under ROOT/build/probe/
set -euo pipefail
root=$1; cpus=$2; name=$3
mkdir -p "$root/content" "$root/build/probe"
rm -rf "$root/content/drsim" "$root/content/probe-results" "$root/content/probe-report.json"
tar --no-same-owner -xzf "$root/build/out/drsim.tar.gz" -C "$root/content"
cp "$root/build/drsim_colab_probe.ipynb" "$root/build/probe/$name.in.ipynb"
taskset -c "$cpus" chroot "$root" /opt/nbenv/bin/jupyter nbconvert --to notebook --execute \
  --ExecutePreprocessor.timeout=1800 --ExecutePreprocessor.kernel_name=python3 \
  --output "/build/probe/$name.ipynb" "/build/probe/$name.in.ipynb"
cp "$root/content/probe-report.json" "$root/build/probe/$name.report.json"
echo "report: $root/build/probe/$name.report.json"
