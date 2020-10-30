#!/bin/sh
set -e

sudo apt install -y python3.8-tk
sudo apt install -y libffi-dev libgirepository1.0-dev libcairo2-dev pkg-config gir1.2-gtk-3.0

# Create virtualenv
virtualenv -p python3.8 env
. env/bin/activate

# Update to latest pip and setuptools
pip install --upgrade pip setuptools

# Install dragonradio package in development mode
(cd .. && CC=gcc-8 pip install python/dragonradio-internal)
(cd .. && pip install -e python/dragonradio)

# Install tool dependencies
pip install 'pycairo<1.20'
pip install --no-build-isolation -Ur requirements.txt
