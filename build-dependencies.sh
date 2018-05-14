#!/bin/sh
set -e

sudo apt install autoconf automake
sudo apt install libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential
sudo apt install libfftw3-dev
sudo apt install h5utils hdf5-helpers hdf5-tools
sudo apt install libhdf5-dev libhdf5-doc libhdf5-cpp-11 libhdf5-cpp-11-dbg
sudo apt install python3-h5py

# Build and install libfec
(cd dependencies/libfec && ./bootstrap && ./configure && make && sudo make install && sudo ldconfig)

# Build and install UHD
(cd dependencies/uhd/host && mkdir -p build && cd build && cmake ../ && make && make test && sudo make install && sudo ldconfig)

# Build and install liquid-dsp
(cd dependencies/liquid-dsp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig)

# Build and install liquid-usrp
(cd dependencies/liquid-usrp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig)

# Install Python dependencies
./build-python-dependencies.sh
