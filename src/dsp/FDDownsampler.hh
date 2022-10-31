// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDDOWNSAMPLER_HH_
#define FDDOWNSAMPLER_HH_

#include <assert.h>

#include <complex>
#include <iostream>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "dsp/FFTW.hh"
#include "dsp/Resample.hh"

namespace dragonradio::signal {

/** @brief An overlap-save frequency domain upsampler
 * @tparam T The type of signal values
 * @tparam P_ The filter length
 * @tparam V_ The overlap factor
 */
template <typename T, unsigned P_=128*3*25+1, unsigned V_=8>
class FDDownsampler : public Resampler<T,T>
{
public:
    using Resampler<T,T>::resample;

    /** @brief Filter length */
    static constexpr unsigned P = P_;

    /** @brief Overlap factor */
    static constexpr unsigned V = V_;

    /** @brief Length of FFT */
    static constexpr unsigned N = V*(P-1);

    /** @brief Size of FFT overlap */
    static constexpr unsigned O = P-1;

    /** @brief Number of new samples consumed per input block */
    static constexpr unsigned L = N - O;

    /** @brief Construct a frequency domain upsampler
     * @param X The oversample factor
     * @param D The decimation factor
     * @param theta The frequency shift (normalized frequency)
     * @param taps FIR filter taps
     */
    FDDownsampler(unsigned X, unsigned D, double theta, const std::vector<T>& taps = {1.0})
      : X(X)
      , D(D)
      , fft_(N, FFTW_FORWARD)
      , ifft_(X*N/D, FFTW_BACKWARD)
      , temp_(N)
      , H_(N)
    {
        if (taps.size() > P) {
            std::stringstream msg;

            msg << "Must have no more that P (" << P << ") taps";

            throw std::range_error(msg.str());
        }

        if (fabs(N*theta - round(N*theta)) > 1e-10) {
            std::stringstream msg;

            msg << "Cannot shift a fractional number of frequency bins: N="
                << N << "; theta=" << theta <<"; bins=" << N*theta;

            throw std::range_error(msg.str());
        }

        if (N % D != 0) {
            std::stringstream msg;

            msg << "Decimation rate " << D
                << " must evenly divide FFT size " << N;

            throw std::range_error(msg.str());
        }

        // Determine number of bins to rotate
        Nrot_ = round(N*theta);
        if (Nrot_ < 0)
            Nrot_ += N;

        // Compute frequency-domain filter
        fftw::FFT<T> fft(N, FFTW_FORWARD);

        std::fill(fft.in.begin(), fft.in.end(), 0);
        std::copy(taps.begin(), taps.end(), fft.in.begin());
        fft.execute(fft.in.data(), H_.data());

        // Compute filter delay
        delay_ = round((taps.size() - 1) / 2.0);

        // Apply 1/(N*D) factor to filter since FFTW doesn't multiply by 1/N for
        // IFFT, and we need to compensate for summation during decimation.
        const T invN = 1.0/N;

        xsimd::transform(H_.begin(), H_.end(), H_.begin(),
            [&](const auto& x) { return x*invN; });
    }

    FDDownsampler() = delete;

    virtual ~FDDownsampler() = default;

    double getRate(void) const override
    {
        return 1.0/D;
    }

    double getDelay(void) const override
    {
        return delay_;
    }

    size_t neededOut(size_t count) const override
    {
        const unsigned Li = X*L/D; // Number of samples produced per input block

        return Li*((count + L - 1)/L);
    }

    void reset() override
    {
    }

    size_t resample(const T* in, size_t count, T* out) override
    {
        return resample(in, count, out, 1.0f);
    }

    /** @brief Resample a signal
     * @param in Input buffer containing signal to downsample
     * @param count Number of input samples
     * @param out Time domain output buffer
     * @param g Gain (linear)
     * @return Number of samples output
     */
    size_t resample(const T* in,
                    size_t count,
                    T* out,
                    float g)
    {
        const unsigned No = X*N/D;
        const unsigned Oo = X*O/D;
        const unsigned Lo = X*L/D;
        size_t inoff = 0;
        size_t fftoff;
        size_t nsamples = 0;

        // Initialize first O samples to zero
        fftoff = O;
        std::fill(fft_.in.begin(), fft_.in.begin() + O, 0);

        while (inoff < count) {
            // Determine how much data is available
            size_t avail = count - inoff;

            // Copy data into FFT buffer
            if (fftoff + avail < N) {
                std::copy(in + inoff,
                          in + inoff + avail,
                          fft_.in.begin() + fftoff);

                std::fill(fft_.in.begin() + fftoff + avail,
                          fft_.in.end(),
                          0);
            } else {
                std::copy(in + inoff,
                          in + inoff + N - fftoff,
                          fft_.in.begin() + fftoff);
            }

            // Perform FFT
            fft_.execute();

            // Downsample the block
            downsampleBlock(fft_.out.data(), ifft_.in.data());

            // Perform IFFT
            ifft_.execute();

            // Copy time domain data to output buffer
            if (fftoff + avail < N) {
                size_t n = X*(fftoff + avail)/D;

                std::copy(ifft_.out.data() + Oo,
                          ifft_.out.data() + n,
                          out + nsamples);

                nsamples += n - Oo;
                break;
            } else {
                std::copy(ifft_.out.data() + Oo,
                          ifft_.out.data() + No,
                          out + nsamples);

                nsamples += Lo;
            }

            // Reset FFT
            inoff += L - fftoff;
            fftoff = 0;
        }

        return nsamples;
    }

    /** @brief Downsample a frequency domain block of data
     * @param in The buffer that contains the input frequency domain data.
     * @param out The buffer that will contain the downsampled frequency domain data.
     */
    void downsampleBlock(const T* in, T* out)
    {
        const unsigned n = N/D;

        /// Shift FFT bins as we copy into temp buffer
        std::rotate_copy(in, in + Nrot_, in + N, temp_.begin());

        // Apply filter
        xsimd::transform(temp_.begin(), temp_.end(), H_.begin(), temp_.begin(),
            [](const auto& x, const auto& y) { return x*y; });

        // Decimate by summing strides of temp buffer, placing result in IFFT
        // input buffer
        std::copy(temp_.begin(), temp_.begin() + n, out);

        for (unsigned i = 1; i < D; ++i)
            xsimd::transform(temp_.begin() + i*n,
                             temp_.begin() + (i+1)*n,
                             out,
                             out,
                             [](const auto& x, const auto& y) { return x+y; });

        // Oversample if needed
        if (X != 1) {
            std::copy(out + n/2,
                      out + n,
                      out + X*n - n/2);
            std::fill(out + n/2,
                      out + n,
                      0);
        }
    }

    /** @brief Incrementally downsample frequency domain data
     * @param in Input buffer containing frequency domain signal to downsample
     * @param count Number of input samples
     * @param f Function to call with downsampled time domain data
     */
    template<class F>
    void downsample(const T* in, size_t count, F&& f)
    {
        for (size_t inoff = 0; inoff < count; inoff += N) {
            // Shift FFT bins as we copy into temp buffer
            size_t avail = count - inoff;

            assert(avail >= N);
            downsampleBlock(in + inoff, temp_.data());

            // Perform IFFT
            ifft_.execute(temp_.data(), ifft_.out.data());

            // Call f with time domain data
            if (avail >= N)
                f(ifft_.out.data() + X*O/D, X*L/D);
            else
                f(ifft_.out.data() + X*O/D, X*(avail - O)/D);
        }
    }

protected:
    /** @brief Oversample factor */
    const unsigned X;

    /** @brief Decimation factor */
    const unsigned D;

    /** @brief Number of bins to rotate */
    int Nrot_;

    /** @brief Filter delay */
    size_t delay_;

    /** @brief FFT */
    fftw::FFT<T> fft_;

    /** @brief Inverse FFT */
    fftw::FFT<T> ifft_;

    /** @brief Vector containing rotated FFT input */
    fftw::vector<T> temp_;

    /** @brief Frequency-domain filter */
    fftw::vector<T> H_;
};

}

#endif /* FDDOWNSAMPLER_HH_ */
