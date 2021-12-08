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
  cd dependencies/libcorrect;
  rm -rf build;
  mkdir build;
  cd build;
  CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" cmake ..;
  make;
  make shim;
  sudo make install;
  sudo ldconfig;
  make clean;
  cd ..;
  rm -rf build
)

# Build and install UHD
(
  cd dependencies/uhd/host;
  rm -rf build;
  mkdir build;
  cd build;
  cmake -DCMAKE_FIND_ROOT_PATH=/usr ../;
  make -j4;
  sudo make install;
  sudo ldconfig;
  make clean
)

# Build and install liquid-dsp
(
  cd dependencies/liquid-dsp;
  ./bootstrap.sh;
  CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" ./configure;
  make;
  sudo make install;
  sudo ldconfig;
  make clean
)

# Build and install firpm
(
  cd dependencies/firpm/firpm_d;
  rm -rf build;
  mkdir build;
  cd build;
  cmake ..;
  make -j4;
  sudo make install;
  sudo ldconfig;
  make clean;
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
pip install -r requirements.txt -e python/dragonradio

# Build dragonradio
make -j10
