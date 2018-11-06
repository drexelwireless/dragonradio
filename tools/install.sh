#!/bin/sh
set -e

sudo apt install -y autoconf automake
sudo apt install -y libboost-all-dev libusb-1.0-0-dev python-mako doxygen python-docutils cmake build-essential libncurses5-dev
sudo apt install -y libfftw3-dev
sudo apt install -y python3 python3-pip python3-tk python3-virtualenv

(
    # Build and install libcorrect
    (cd ../dependencies/libcorrect && rm -rf build && mkdir build && cd build && cmake .. && make && make shim && sudo make install && sudo ldconfig && make clean && cd .. && rm -rf build)

    # Build and install liquid-dsp
    (cd ../dependencies/liquid-dsp && ./bootstrap.sh && ./configure && make && sudo make install && sudo ldconfig && make clean)
)

virtualenv -p python3 env
. env/bin/activate

pip install -Ur requirements.txt

sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install gcc-7 g++-7

(
    cd modules/dragonradio
    CC=gcc-7 python3 setup.py build
    python3 setup.py install
)