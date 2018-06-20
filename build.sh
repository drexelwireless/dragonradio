#!/bin/sh
set -e

sudo apt install -y autoconf automake
sudo apt install -y libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential
sudo apt install -y libfftw3-dev
sudo apt install -y libhdf5-dev libhdf5-doc libhdf5-cpp-11 libhdf5-cpp-11-dbg
sudo apt install -y python3 python3-pip python3-h5py

# Build and install libfec
(cd dependencies/libfec && ./bootstrap && ./configure && make && sudo make install && sudo ldconfig && make clean)

# Build and install UHD
(cd dependencies/uhd/host && mkdir -p build && cd build && cmake ../ && make -j4 && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-dsp
(cd dependencies/liquid-dsp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-usrp
(cd dependencies/liquid-usrp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig && make clean)

# Install Python dependencies
sudo pip3 install --upgrade pip
sudo pip3 install -r python/requirements.txt

# Build dragonradio
make
