#!/bin/sh
set -e

sudo apt install -y autoconf automake
sudo apt install -y libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential libncurses5-dev
sudo apt install -y libfftw3-dev
sudo apt install -y libhdf5-dev libhdf5-doc libhdf5-cpp-11 libhdf5-cpp-11-dbg
sudo apt install -y python3 python3-pip

# Build and install libcorrect
(cd dependencies/libcorrect && rm -rf build && mkdir build && cd build && cmake .. && make && make shim && sudo make install && sudo ldconfig && make clean && cd .. && rm -rf build)

# Build and install UHD
(cd dependencies/uhd/host && rm -rf build && mkdir build && cd build && cmake ../ && make -j4 && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-dsp
(cd dependencies/liquid-dsp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig && make clean)

# Build and install liquid-usrp
(cd dependencies/liquid-usrp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig && make clean)

# Install Python dependencies
sudo pip3 install --upgrade pip
sudo pip3 install -Ur python/requirements.txt

# Build dragonradio
make
