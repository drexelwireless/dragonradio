// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "dsp/FFTW.hh"

std::mutex fftw::mutex;

void fftw::planFFTs(unsigned N)
{
    using T = std::complex<float>;

    fftw::FFT<T> fft(N, FFTW_FORWARD);
    fftw::FFT<T> ifft(N, FFTW_BACKWARD);
}
