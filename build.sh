#!/bin/sh
set -e

sudo apt install -y autoconf automake
sudo apt install -y libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential libncurses5-dev
sudo apt install -y libfftw3-dev
sudo apt install -y libhdf5-dev libhdf5-doc libhdf5-cpp-11 libhdf5-cpp-11-dbg
sudo apt install -y python3 python3-pip
sudo apt install -y libflac8 libflac-dev libflac++-dev

sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install gcc-8 g++-8

CC=gcc-8
CXX=g++-8
CFLAGS="-Ofast -march=native"

# Build and install libcorrect
(cd dependencies/libcorrect && rm -rf build && mkdir build && cd build && CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" cmake .. && make && make shim && sudo make install && sudo ldconfig && make clean && cd .. && rm -rf build)

# Build and install UHD
(cd dependencies/uhd/host && rm -rf build && mkdir build && cd build && cmake ../ && make -j4 && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-dsp
(cd dependencies/liquid-dsp && ./bootstrap.sh && CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" ./configure && make && sudo make install && sudo ldconfig && make clean)

# Install Python dependencies
sudo pip3 install --upgrade pip
sudo pip3 install -Ur python/requirements.txt

# Build dragonradio
make
