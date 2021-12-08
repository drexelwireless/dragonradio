#!/bin/sh
set -e

sudo apt install -y autoconf automake build-essential cmake
sudo apt install -y python3.8-tk
sudo apt install -y libffi-dev libgirepository1.0-dev libcairo2-dev pkg-config gir1.2-gtk-3.0
sudo apt install -y libsnappy-dev
sudo apt install -y libfftw3-dev
sudo apt install -y libflac8 libflac-dev libflac++-dev
sudo apt install -y libeigen3-dev

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
