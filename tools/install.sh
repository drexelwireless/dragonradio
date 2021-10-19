#!/bin/sh
set -e

sudo apt install -y python3.8-tk
sudo apt install -y libffi-dev libgirepository1.0-dev libcairo2-dev pkg-config gir1.2-gtk-3.0
sudo apt install -y libsnappy-dev

# Create virtualenv
virtualenv -p python3.8 venv
. venv/bin/activate

# Update to latest pip and setuptools
pip install --upgrade pip setuptools

# Install dragonradio package in development mode
(cd .. && pip install python/dragonradio-internal)
(cd .. && pip install -e python/dragonradio)
(cd .. && pip install -e python/dragonradio-tools)
