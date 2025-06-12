#!/bin/sh
set -e

while getopts "d" arg; do
  case "$arg" in
    d)
      EXTRA="[dev]"
      ;;
    *)
      ;;
  esac
done

sudo apt install -y python3-tk
sudo apt install -y gir1.2-gtk-3.0 libcairo2-dev libffi-dev libgirepository-2.0-dev pkg-config
sudo apt install -y libsnappy-dev

# Create virtualenv
rm -rf venv
virtualenv -p python3 venv
. venv/bin/activate

# Update to latest pip and setuptools
pip install --upgrade pip setuptools wheel

# Install dragonradio package in development mode
BASEDIR=$(dirname "$0")

pip install -r requirements.txt \
  "$BASEDIR/.." \
  "$BASEDIR/../python/dragonradiocpp-stubs" \
  -e "$BASEDIR/../python/dragonradio${EXTRA}" \
  -e "$BASEDIR/../python/dragonradio-tools"
