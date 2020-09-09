#!/bin/sh
set -e

sudo apt install -y python3-tk
sudo apt install -y libffi-dev libgirepository1.0-dev libcairo2-dev pkg-config gir1.2-gtk-3.0

# Create virtualenv
virtualenv -p python3 env
. env/bin/activate

# Install dragonradio package in development mode
(cd .. && CC=gcc-8 pip install python/dragonradio-internal)
(cd .. && pip install -e python/dragonradio)

# Install tool dependencies
pip install -Ur requirements.txt
