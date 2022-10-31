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

void fftw::exportWisdom(const std::string& path)
{
    std::lock_guard<std::mutex> lock(fftw::mutex);

    if (fftwf_export_wisdom_to_filename(path.c_str()) == 0)
        throw std::runtime_error("Could not export wisdom");
}

void fftw::importWisdom(const std::string& path)
{
    std::lock_guard<std::mutex> lock(fftw::mutex);

    if (fftwf_import_wisdom_from_filename(path.c_str()) == 0)
        throw std::runtime_error("Could not import wisdom");
}
