#!/bin/bash
# Create the Ubuntu 22.04 (jammy) root used by build-colab-bundle.sh and
# run-colab-probe.sh. Colab runtimes are Ubuntu 22.04, so a bundle built here
# needs nothing newer than glibc 2.35. Needs root (sudo) on the host; tested in
# the gridgpu01 LXD container (Ubuntu 24.04).
# Usage: sudo colab-chroot.sh ROOT        e.g. /tmp/colab-jammy
set -euo pipefail
R=$1
PBS_URL=${PBS_URL:-https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-x86_64-unknown-linux-gnu-install_only.tar.gz}
command -v debootstrap >/dev/null || DEBIAN_FRONTEND=noninteractive apt-get install -y -q debootstrap
[ -e "$R/etc/os-release" ] || debootstrap --variant=buildd --components=main,universe jammy "$R" http://archive.ubuntu.com/ubuntu
cat > "$R/etc/apt/sources.list" <<EOF
deb http://archive.ubuntu.com/ubuntu jammy main universe
deb http://archive.ubuntu.com/ubuntu jammy-updates main universe
deb http://security.ubuntu.com/ubuntu jammy-security main universe
EOF
cp /etc/resolv.conf "$R/etc/resolv.conf"
# Recursive bind for /dev: in an LXD container the device nodes are separate
# bind mounts, and a plain bind exposes empty placeholders instead.
mountpoint -q "$R/proc" || mount --bind /proc "$R/proc"
mountpoint -q "$R/sys"  || mount --bind /sys "$R/sys"
mountpoint -q "$R/dev"  || mount --rbind /dev "$R/dev"
chroot "$R" bash -c "export DEBIAN_FRONTEND=noninteractive; apt-get update -q && apt-get install -y -q --no-install-recommends \
  build-essential git cmake autoconf automake libtool pkg-config curl ca-certificates libuhd-dev libboost-all-dev \
  libhdf5-dev libflac++-dev libcap-dev libfftw3-dev libeigen3-dev protobuf-compiler patchelf file"
# Standalone CPython 3.12 (shared libpython): embedded by dragonradio and shipped in the bundle.
if [ ! -x "$R/opt/python/bin/python3" ]; then
  mkdir -p "$R/opt"; curl -sfL "$PBS_URL" | tar --no-same-owner -xzf - -C "$R/opt"
fi
# Jupyter for executing the probe notebook headless (run-colab-probe.sh).
[ -x "$R/opt/nbenv/bin/jupyter" ] || chroot "$R" bash -c "/opt/python/bin/python3 -m venv /opt/nbenv && \
  /opt/nbenv/bin/pip install -q nbconvert nbclient ipykernel matplotlib pandas psutil numpy && \
  /opt/nbenv/bin/python -m ipykernel install --name python3 --prefix /opt/nbenv"
mkdir -p "$R/build"
echo "ready: copy SOURCE.tar.gz and LIQUID.tar.gz to $R/build, then"
echo "  sudo chroot $R bash /build/build-colab-bundle.sh /build/src.tar.gz /build/liquid.tar.gz /build/out"
