#!/bin/sh
set -e

umask 022

sudo apt install -y autoconf automake build-essential cmake
sudo apt install -y python3-mako
sudo apt install -y libboost-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-regex-dev libboost-system-dev libboost-serialization-dev libboost-test-dev libboost-thread-dev
sudo apt install -y libfftw3-dev
sudo apt install -y libhdf5-dev
sudo apt install -y libflac8 libflac-dev libflac++-dev
sudo apt install -y libeigen3-dev
sudo apt install -y ninja-build

# For capabilities
sudo apt install -y libcap-dev

# Install Python 3.8
sudo apt install -y python3 python3-dev python3-distutils python3-pip

# Install virtualenv
sudo apt install -y python3-virtualenv virtualenv

CC=gcc
CXX=g++
CFLAGS="-Ofast -march=native"

# Build and install libcorrect
(
  cd extern/libcorrect;
  rm -rf build;
  mkdir build;
  cd build;
  CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" cmake -G Ninja ..;
  ninja;
  ninja shim;
  sudo ninja install;
  sudo ldconfig;
  ninja clean;
  cd ..;
  rm -rf build
)

# Build and install UHD
(
  cd extern/uhd/host;
  rm -rf build;
  mkdir build;
  cd build;
  cmake -DCMAKE_FIND_ROOT_PATH=/usr -G Ninja ..;
  ninja;
  sudo ninja install;
  sudo ldconfig;
  rm -rf docs/doxygen/html;
  ninja clean
)

# Download firmware
sudo apt install -y python-is-python3 python3-requests
sudo pip install --upgrade urllib3
sudo uhd_images_downloader

# Build and install liquid-dsp
(
  cd extern/liquid-dsp;
  ./bootstrap.sh;
  CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" ./configure;
  make;
  sudo make install;
  sudo ldconfig;
  make clean
)

# Build and install firpm
(
  cd extern/firpm;
  rm -rf build;
  mkdir build;
  cd build;
  cmake -G Ninja ..;
  ninja;
  sudo ninja install;
  sudo ldconfig;
  ninja clean;
  cd ..;
  rm -rf build
)

# Create virtualenv
rm -rf venv
virtualenv -p python3.8 venv
. venv/bin/activate

# Update to latest pip and setuptools
pip install --upgrade pip setuptools wheel

# Install dragonradio package in development mode
#
# XXX We must use the system HDF5 library to avoid linking conflicts when
# embedding in dragonradio, so we set PIP_NO_BINARY=tables to ensure the tables
# spackage is built from source.
PIP_NO_BINARY=tables pip install -r requirements.txt -e python/dragonradio

# Build dragonradio
(
  rm -rf build;
  mkdir build;
  cd build;
  CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" cmake ..;
  make -j10;
  cd ..;
  ln -sf build/dragonradio;
  ln -sf ../../build/dragonradio venv/bin/dragonpython
)
