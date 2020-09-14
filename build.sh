#!/bin/sh
set -e

umask 022

sudo apt install -y autoconf automake
sudo apt install -y libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential libncurses5-dev
sudo apt install -y libfftw3-dev
sudo apt install -y libhdf5-dev libhdf5-doc libhdf5-cpp-11 libhdf5-cpp-11-dbg
sudo apt install -y libflac8 libflac-dev libflac++-dev
sudo apt install -y libeigen3-dev

sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install -y gcc-8 g++-8

# Install Python 3.8
sudo add-apt-repository -y ppa:deadsnakes/ppa
sudo apt update
sudo apt install -y python3.8 python3.8-dev python3.8-distutils

# Install pip in Python 3.8
curl https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
sudo python3.8 /tmp/get-pip.py

# Remove old virtualenv packages
sudo apt remove -y python3-virtualenv virtualenv

# Install new virtualenv packages. This won't work unless we first remove the
# existing virtualenv.
sudo python3.8 -m pip install virtualenv

CC=gcc-8
CXX=g++-8
CFLAGS="-Ofast -march=native"

# Build and install libcorrect
(cd dependencies/libcorrect && rm -rf build && mkdir build && cd build && CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" cmake .. && make && make shim && sudo make install && sudo ldconfig && make clean && cd .. && rm -rf build)

# Build and install UHD
(cd dependencies/uhd/host && rm -rf build && mkdir build && cd build && cmake ../ && make -j4 && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-dsp
(cd dependencies/liquid-dsp && ./bootstrap.sh && CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" ./configure && make && sudo make install && sudo ldconfig && make clean)

# Build and install firpm
(cd dependencies/firpm/firpm_d && rm -rf build && mkdir build && cd build && cmake .. && make -j4 && sudo make install && sudo ldconfig && make clean && cd .. && rm -rf build)

# Create virtualenv
virtualenv -p python3.8 env
. env/bin/activate

# Update to latest pip and setuptools
pip install --upgrade pip setuptools

# Install dragonradio package in development mode
pip install -e python/dragonradio

# Build dragonradio
make -j10
