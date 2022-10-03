// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDRESAMPLER_HH_
#define FDRESAMPLER_HH_

#include <assert.h>

#include <complex>
#include <iostream>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "dsp/fftcopy.hh"
#include "dsp/fmcopy.hh"
#include "dsp/FFTW.hh"
#include "dsp/Resample.hh"

namespace dragonradio::signal {

/** @brief An overlap-save frequency domain resampler
 * @tparam T The type of signal values
 * @tparam P_ The filter length
 * @tparam V_ The overlap factor
 */
template <typename T, unsigned P_=128*3*25+1, unsigned V_=8>
class FDResampler : public RationalResampler<T,T>
{
public:
    using RationalResampler<T,T>::resample;

    /** @brief Filter length */
    static constexpr unsigned P = P_;

    /** @brief Overlap factor */
    static constexpr unsigned V = V_;

    /** @brief Length of FFT */
    static constexpr unsigned N = V*(P-1);

    /** @brief Size of FFT overlap */
    static constexpr unsigned O = P-1;

    /** @brief Number of new samples consumed per input block */
    static constexpr unsigned L = N-O;

    /** @brief Construct a frequency domain upsampler
     * @param I The interpolation factor
     * @param D The decimation factor
     * @param X The oversample factor
     * @param theta The frequency shift (normalized frequency)
     * @param taps FIR filter taps
     */
    FDResampler(unsigned I_, unsigned D_, unsigned X_ = 1, double theta = 0.0, const std::vector<T>& taps = {1.0})
      : I(I_)
      , D(D_)
      , X(X_)
      , exact_(false)
      , parallel_(false)
      , fft_(_i(N), FFTW_FORWARD, FFTW_MEASURE)
      , ifft_(_o(N), FFTW_BACKWARD, FFTW_MEASURE)
      , H_(I*_i(N))
      , temp_(I*_i(N))
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

        if (N % I != 0) {
            std::stringstream msg;

            msg << "Interpolation rate " << I
                << " must evenly divide FFT size " << N;

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
        fftw::FFT<T> fft(I*_i(N), FFTW_FORWARD, FFTW_MEASURE);

        std::fill(fft.in.begin(), fft.in.end(), 0);
        std::copy(taps.begin(), taps.end(), fft.in.begin());
        fft.execute(fft.in.data(), H_.data());

        // Compute filter delay, compensating for trailing zeros in taps
        unsigned ntaps = taps.size();

        while (ntaps > 0 && taps[ntaps-1] == static_cast<T>(0))
            --ntaps;

        if (ntaps == 0)
            throw std::range_error("Filter taps must be non-empty");

        delay_ = round((ntaps - 1.0) / 2.0);

        // Apply 1/N factor to filter since FFTW doesn't multiply by 1/N for
        // IFFT.
        const T invN = 1.0/_i(N);

        xsimd::transform(H_.begin(), H_.end(), H_.begin(),
            [&](const auto& x) { return x*invN; });

        // Reset resampler state
        reset();
    }

    FDResampler() = delete;

    virtual ~FDResampler() = default;

    /** @brief Is upsampling exact? */
    bool getExact(void) const
    {
        return exact_;
    }

    /** @brief Make upsampling exact */
    void setExact(bool exact)
    {
        exact_ = exact;
    }

    /** @brief Is upsampling parallelizable? */
    bool getParallelizable(void) const
    {
        return parallel_;
    }

    /** @brief Make upsampling parallelizable */
    void setParallelizable(bool parallel)
    {
        parallel_ = parallel;
    }

    double getDelay(void) const override
    {
        return exact_ ? 0.0 : delay_;
    }

    size_t neededOut(size_t count) const override
    {
        const unsigned Li = _i(L);
        const unsigned Lo = _o(L);

        return Lo*((count + Li - 1)/Li);
    }

    unsigned getInterpolationRate(void) const override
    {
        return I;
    }

    unsigned getDecimationRate(void) const override
    {
        return D;
    }

    void reset(void) override
    {
        reset(0);
    }

    size_t resample(const C *in, size_t count, C *out) override
    {
        return resample(in, count, out, 1.0);
    }

    /** @brief Reset the resampler state
     * @param offset The offset at which samples should be output
     */
    /** The first offset samples output will be zero */
    void reset(size_t offset)
    {
        const unsigned Oi = _i(O);

        fftoff_ = Oi + offset;
        assert(fftoff_ <= fft_.in.size());
        std::fill(fft_.in.begin(), fft_.in.begin() + fftoff_, 0);
    }

    /** @brief Return number of pending output samples in buffer */
    size_t npending(void) const
    {
        const unsigned Oo = _o(O);
        size_t         n = I*fftoff_/D;

        return n > Oo ? n - Oo : 0;
    }

    /** @brief Save FFT offset
     * @return Current FFT offset
     */
    std::optional<size_t> saveFFTOffset() const
    {
        return fftoff_;
    }

    /** @brief Restore FFT offset
     * @param fftoff New FFT offset
     */
    void restoreFFTOffset(size_t fftoff)
    {
        fftoff_ = fftoff;
    }

    /** @brief Copy most recent FFT output block
     * @param out Frequency domain buffer
     */
    void copyFFTOut(T* out)
    {
        resampleBlock(fft_.out.data(), out);
    }

    /** @brief Resample time domain data
     * @param in Input buffer containing signal to resample
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
        const unsigned Ni = _i(N);
        const unsigned Oi = _i(O);
        const unsigned Li = _i(L);
        const unsigned No = _o(N);
        const unsigned Oo = _o(O);
        const unsigned Lo = _o(L);

        size_t inoff = 0;
        size_t nsamples = 0;

        // Initialize first Oi samples to zero
        fftoff_ = Oi;
        std::fill(fft_.in.begin(), fft_.in.begin() + Oi, 0);

        while (inoff < count) {
            // Determine how much data is available
            size_t avail = count - inoff;

            // Copy data into FFT buffer, multiplying by g if it is not unity.
            if (fftoff_ + avail < Ni) {
                fmcopy(in + inoff,
                       in + inoff + avail,
                       fft_.in.begin() + fftoff_,
                       g);

                std::fill(fft_.in.begin() + fftoff_ + avail,
                          fft_.in.end(),
                          0);
            } else {
                fmcopy(in + inoff,
                       in + inoff + Ni - fftoff_,
                       fft_.in.begin() + fftoff_,
                       g);
            }

            // Perform FFT
            fft_.execute();

            // Resample the block
            resampleBlock(fft_.out.data(), ifft_.in.data());

            // Perform IFFT
            ifft_.execute();

            // Copy time domain data to output buffer
            if (fftoff_ + avail < Ni) {
                size_t n = I*(fftoff_ + avail)/D;

                std::copy(ifft_.out.begin() + Oo,
                          ifft_.out.begin() + n,
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
            inoff += Li - fftoff_;
            fftoff_ = 0;
        }

        return nsamples;
    }

    /** @brief Resample a frequency domain block of data
     * @param in The buffer that contains the input frequency domain data.
     * @param out The buffer that will contain the resampled frequency domain data.
     */
    void resampleBlock(const T* in, T* out)
    {
        const unsigned Ni = _i(N);
        const unsigned No = _o(N);

        if (exact_) {
            if (I > D) {
                // If we are performing exact upsampling, copy the bottom and
                // top halves of the input FFT directly to the output FFT. Since
                // we are upsampling, we are guaranteed that the size of the
                // input FFT is less that the size of the output FFT (see the
                // definition of _i and the associated comments below), so we
                // are also guaranteed that the respective destinations of the
                // bottom and top halves of the input FFT do not overlap. We
                // must multiply by 1/Ni to compensate for the input FFT.
                fftmixup<T>(in, Ni, Ni/X, out, No, Nrot_, 1.0/Ni);
            } else {
                fftmixdown<T>(in, Ni, No/X, Nrot_, out, No, 1.0/Ni);
            }
        } else {
            // If we are downsampling, mix down by shifting FFT bins left as we copy
            // into temp buffer. Otherwise, copy data directly.
            if (D > I) {
                assert(Ni == N);

                std::rotate_copy(in, in + Nrot_, in + N, temp_.begin());
            } else
                std::copy(in, in + Ni, temp_.begin());

            // Duplicate first block of Ni samples I times
            for (unsigned i = 1; i < I; ++i)
                std::copy(temp_.begin(), temp_.begin() + Ni, temp_.begin() + i*Ni);

            // Apply filter
            xsimd::transform(temp_.begin(), temp_.end(), H_.begin(), temp_.begin(),
                [](const auto& x, const auto& y) { return x*y; });

            // Decimate by summing strides of temp buffer
            const unsigned n = I*_i(N)/D;

            for (unsigned i = 1; i < D; ++i)
                xsimd::transform(temp_.begin(),
                                 temp_.begin() + n,
                                 temp_.begin() + i*n,
                                 temp_.begin(),
                                 [](const auto& x, const auto& y) { return x+y; });

            if (parallel_ && I > D) {
                fftmixup<T>(temp_.data(), N, Ni/X, out, N, Nrot_, 1.0);
            } else {
                // If we are upsampling, mix up by shifting FFT bins in the
                // output buffer. Since std::rotate shifts left, we must shift
                // by N - Nrot_ bins to shift "right" by Nrot_ bins. Otherwise,
                // copy temp buffer directly to output.
                if (I > D && Nrot_ != 0) {
                    assert(No == N);

                    std::rotate_copy(temp_.begin(),
                                     temp_.begin() + N - Nrot_,
                                     temp_.begin() + N,
                                     out);
                } else
                    std::copy(temp_.begin(), temp_.begin() + n, out);
            }
        }
    }

    /** @brief Resample frequency domain data
     * @param in Input buffer containing frequency domain signal to resample
     * @param count Number of input samples
     * @param f Function to call with resampled time domain data
     */
    template<class F>
    void resampleFromFD(const T* in, size_t count, F&& f)
    {
        const unsigned Ni = _i(N);
        const unsigned Oo = _o(O);
        const unsigned Lo = _o(L);

        for (size_t inoff = 0; inoff < count; inoff += Ni) {
            assert(count - inoff >= Ni);

            // Resample input FFT block as we copy frequency domain signal to
            // IFFT input buffer.
            resampleBlock(in + inoff, ifft_.in.data());

            // Perform IFFT
            ifft_.execute();

            // Call f with time domain data
            f(ifft_.out.data() + Oo, Lo);
        }
    }

    /** @brief Resample time domain data to produce intermediate frequency domain data
     * @param in Time domain signal to resample
     * @param count Number of input samples
     * @param out Frequency domain output buffer
     * @param g Gain (linear)
     * @param flush Flush remaining samples in FFT with zeros (no more signal)
     * @param f Function to call when output samples are generated
     * @return Offset of first unconsumed sample in input buffer
     */
    template<class F>
    size_t resampleToFD(const T* in,
                        size_t count,
                        T* out,
                        const float g,
                        bool flush,
                        F&& f)
    {
        const unsigned Ni = _i(N);
        const unsigned Li = _i(L);
        const unsigned Oi = _i(O);
        const unsigned No = _o(N);
        const unsigned Oo = _o(O);

        size_t inoff = 0;  // Offset into input buffer

        // We must allow inoff == count here to allow the upsampler to be
        // flushed *without* requiring additional samples.
        while (inoff <= count) {
            size_t avail = count - inoff;

            // If we don't have enough samples for a full FFT block...
            if (fftoff_ + avail < Ni) {
                fmcopy(in + inoff,
                       in + inoff + avail,
                       fft_.in.begin() + fftoff_,
                       g);

                // If we are flushing the upsampler and we have some signal
                // pending, fill the rest of FFT block with zeros.
                if (flush && fftoff_ + avail > Oi) {
                    std::fill(fft_.in.begin() + fftoff_ + avail,
                              fft_.in.end(),
                              0);
                // We're not flushing, so return immediately so we can process a
                // full block later when more data is available.
                } else {
                    inoff += avail;
                    fftoff_ += avail;
                    return inoff;
                }
            } else {
                fmcopy(in + inoff,
                       in + inoff + Ni - fftoff_,
                       fft_.in.begin() + fftoff_,
                       g);
            }

            // Perform the FFT
            fft_.execute();

            // Resample the block, copying to output
            resampleBlock(fft_.out.data(), out);
            out += No;

            // If we flushed a partial block, return. If fftoff_ + avail < Ni,
            // we must be flushing because if flush were false, we would have
            // returned above instead of filling the buffer with zeroes.
            if (fftoff_ + avail < Ni) {
                size_t n = I*(fftoff_ + avail)/D;

                inoff += avail;
                fftoff_ = 0;

                f(n - Oo);

                break;
            // If the FFT buffer held up to Li samples, we can get all the
            // overlap data we need for the next FFT from the input buffer.
            } else if (fftoff_ <= Li) {
                inoff += Li - fftoff_;
                fftoff_ = 0;

                if (!f(L))
                    break;
            // Otherwise, we need to reuse some of the data in the current FFT
            // buffer for the overlap.
            } else {
                std::copy(fft_.in.begin() + Li,
                          fft_.in.end(),
                          fft_.in.begin());
                fftoff_ -= Li;

                if (!f(L))
                    break;
            }
        }

        return inoff;
    }

    class ToTimeDomain
    {
    public:
        ToTimeDomain(void)
          : ifft(N, FFTW_BACKWARD, FFTW_MEASURE)
        {
        }

        size_t toTimeDomain(const T* in, size_t count, T* out)
        {
            size_t outoff = 0;

            while (count >= N) {
                // Copy data into IFFT buffer
                std::copy(in, in + N, ifft.in.begin());

                // Perform IFFT
                ifft.execute();

                // Copy time-domain data into IQ buffer
                std::copy(ifft.out.begin() + O,
                          ifft.out.end(),
                          out + outoff);

                count -= N;
                in += N;
                outoff += L;
            }

            return outoff;
        }

        /** @brief FFT */
        fftw::FFT<T> ifft;
    };

protected:
    /** @brief Interpolation factor */
    const unsigned I;

    /** @brief Decimation factor */
    const unsigned D;

    /** @brief Oversample factor */
    const unsigned X;

    /** @brief Number of bins to rotate */
    int Nrot_;

    /** @brief Filter delay */
    size_t delay_;

    /** @brief Should upsampling be exact */
    /** Exact upsampling copies FFT bins directly */
    bool exact_;

    /** @brief Should upsampling be parallelizable? */
    /** Parallelizable upsampling copies only the primary FFT bins. This allows
     * multiple channels to be synthesize into the same destination FFT in
     * parallel. */
    bool parallel_;

    /** @brief FFT */
    fftw::FFT<T> fft_;

    /** @brief Offset into FFT input at which to place new data */
    size_t fftoff_;

    /** @brief Inverse FFT */
    fftw::FFT<T> ifft_;

    /** @brief Frequency-domain filter */
    fftw::vector<T> H_;

    /** @brief Vector containing rotated FFT input */
    fftw::vector<T> temp_;

    //  If we are upsampling:
    //    * The input FFT should have size N*D/*I
    //    * The output FFT should have size N.
    //  If we are downsampling:
    //    * The input FFT should have size N.
    //    * The output FFT should have size I*N/D.

    /** @brief Convert an FFT paramater to an input FFT parameter */
    template <class U>
    U _i(U N) const
    {
        return I > D ? N*D/I : N;
    }

    /** @brief Convert an FFT paramater to an output FFT parameter */
    template <class U>
    U _o(U N) const
    {
        return I > D ? N : I*N/D;
    }
};

}

#endif /* FDRESAMPLER_HH_ */
