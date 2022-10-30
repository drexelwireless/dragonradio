// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDUPSAMPLER_HH_
#define FDUPSAMPLER_HH_

#include <assert.h>

#include <complex>

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
class FDUpsampler : public Resampler<T,T>
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
    static constexpr unsigned L = N-O;

    /** @brief Construct a frequency domain upsampler
     * @param X_ The oversample factor
     * @param I_ The interpolation factor
     * @param theta The frequency shift (normalized frequency)
     */
    FDUpsampler(unsigned X_, unsigned I_, double theta)
      : X(X_)
      , I(I_)
      , fft(X*N/I, FFTW_FORWARD)
      , ifft(N, FFTW_BACKWARD)
    {
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

        // Determine number of bins to rotate
        Nrot = round(N*theta);
        if (Nrot < 0)
            Nrot += N;

        reset();
    }

    FDUpsampler() = delete;

    virtual ~FDUpsampler() = default;

    double getRate(void) const override
    {
        return I;
    }

    double getDelay(void) const override
    {
        return 0;
    }

    size_t neededOut(size_t count) const override
    {
        const unsigned Li = X*L/I; // Number of samples consumed per input block

        return L*((count + Li - 1)/Li);
    }

    void reset() override
    {
        reset(0);
    }

    size_t resample(const T* in, size_t count, T* out) override
    {
        return resample(in, count, out, 1.0f);
    }

    /** @brief Reset the upsampler state
     * @param offset The offset at which samples should be output
     */
    /** The first offset samples output will be zero */
    void reset(size_t offset)
    {
        const unsigned Oi = X*O/I; // Overlap factor for input FFT

        fftoff = Oi + offset;
        std::fill(fft.in.begin(), fft.in.begin() + fftoff, 0);
    }

    /** @brief Upsample a frequency domain block of data
     * @param in The buffer that contains the input frequency domain data.
     * @param out The buffer that will contain the upsampled frequency domain data.
     */
    void upsampleBlock(const T* in, T* out)
    {
        const unsigned Ni = X*N/I; // Size of forward FFT for input
        const int      n = N/I;    // Size of input block, not counting
                                   // oversampling
        auto           temp = in[n/2] / 2.0f;

        // Copy FFT buffer to out, upsampling and frequency shifting by rotating
        // bins. Note that we don't copy bins that result from oversampling on
        // the part of the modulator.
        //
        // Since N is always even, we need to split the bin at the Nyquist
        // frequency.
        assert(Nrot >= 0);

        if (Nrot == 0) {
            std::copy(in,
                      in + n/2,
                      out);
            std::copy(in + Ni - n/2 + 1,
                      in + Ni,
                      out + N - n/2 + 1);

            out[n/2] += temp;
            out[N - n/2] = temp;
        } else {
            std::copy(in,
                      in + n/2,
                      out + Nrot);
            std::copy(in + Ni - n/2 + 1,
                      in + Ni,
                      out + Nrot - n/2 + 1);

            out[Nrot + n/2] += temp;
            out[Nrot - n/2] = temp;
        }
    }

    /** @brief Resample a signal
     * @param in Input signal
     * @param count Number of input samples
     * @param out Buffer for upsampled signal
     * @param g Gain (linear)
     * @return Number of samples output
     */
    size_t resample(const T* in,
                    size_t count,
                    T* out,
                    float g)
    {
        const unsigned Ni = X*N/I;   // Size of forward FFT for input
        const unsigned Li = X*L/I;   // Number of samples consumed per input block
        size_t         inoff = 0;    // Offset into input buffer
        size_t         nsamples = 0; // Number of samples output

        // The upsampled signal is multiplied by this constant. It incorporates:
        //   * The requested gain
        //   * Scaling compensation for the FFT
        const float k = g/static_cast<float>(Ni);

        // Reset upsampler state
        reset();

        // Set all upsampled FFT bins to 0. We only copy values into a subset of
        // the bins, so this ensures the rest are 0.
        std::fill(ifft.in.begin(), ifft.in.end(), 0);

        while (inoff < count) {
            size_t avail = count - inoff;

            if (fftoff + avail < Ni) {
                std::copy(in + inoff,
                          in + inoff + avail,
                          fft.in.begin() + fftoff);

                std::fill(fft.in.begin() + fftoff + avail,
                          fft.in.end(),
                          0);
            } else {
                std::copy(in + inoff,
                          in + inoff + Ni - fftoff,
                          fft.in.begin() + fftoff);
            }

            // Perform the FFT
            fft.execute();

            // Normalize by k
            xsimd::transform(fft.out.begin(),
                             fft.out.end(),
                             fft.out.begin(),
                [k](const auto& x) { return x*k; });

            // Copy FFT buffer to output, upsampling and frequency shifting by
            // shifting bins.
            upsampleBlock(fft.out.data(), ifft.in.data());

            // Perform inverse FFT to convert back to time domain
            ifft.execute();

            // Copy time-domain data into IQ buffer
            if (fftoff + avail < Ni) {
                std::copy(ifft.out.begin() + O,
                          ifft.out.begin() + I*(fftoff + avail)/X,
                          out + nsamples);

                nsamples += I*(fftoff + avail)/X - O;

                break;
            } else {
                std::copy(ifft.out.begin() + O,
                          ifft.out.end(),
                          out + nsamples);

                nsamples += L;
                inoff += Li - fftoff;
                fftoff = 0;
            }
        }

        //assert(I*count/X == nsamples);
        return nsamples;
    }

    /** @brief Incrementally upsample time domain data to produce frequency domain data
     * @param in Input buffer containing signal to upsample
     * @param count Number of input samples
     * @param out Frequency domain output buffer
     * @param g Gain (linear)
     * @param flush Flush remaining samples in FFT with zeros (no more signal)
     * @param f Function to call when output samples are generated
     * @return Offset of first unconsumed sample in input buffer
     */
    template<class F>
    size_t upsample(const T* in,
                    size_t count,
                    T* out,
                    const float g,
                    bool flush,
                    F&& f)
    {
        const unsigned Ni = X*N/I; // Size of forward FFT for input
        const unsigned Li = X*L/I; // Number of samples consumed per input block
        size_t         inoff = 0;  // Offset into input buffer

        // The upsampled signal is multiplied by this constant. It incorporates:
        //   * The requested gain
        //   * Scaling compensation for the FFT
        const float k = g/static_cast<float>(Ni);

        // We must allow inoff == count here to allow the upsampler to be
        // flushed *without* requiring additional samples.
        while (inoff <= count) {
            size_t avail = count - inoff;

            // If we don't have enough samples for a full FFT block...
            if (fftoff + avail < Ni) {
                std::copy(in + inoff,
                          in + inoff + avail,
                          fft.in.begin() + fftoff);

                // If we are flushing the signal, fill the rest of FFT block
                // with zeros. Otherwise, return immediately so we can process a
                // full block when more data is available.
                if (flush) {
                    std::fill(fft.in.begin() + fftoff + avail,
                              fft.in.end(),
                              0);
                } else {
                    inoff += avail;
                    fftoff += avail;
                    return inoff;
                }
            } else {
                std::copy(in + inoff,
                          in + inoff + Ni - fftoff,
                          fft.in.begin() + fftoff);
            }

            // Perform the FFT
            fft.execute();

            // Normalize by k
            xsimd::transform(fft.out.begin(),
                             fft.out.end(),
                             fft.out.begin(),
                [k](const auto& x) { return x*k; });

            // Copy FFT buffer to output, upsampling and frequency shifting by
            // shifting bins.
            upsampleBlock(fft.out.data(), out);
            out += N;

            // If we flushed a partial block, return.
            //
            // If the FFT buffer held up to Li samples, we can get all the
            // overlap data we need for the next FFT from the input buffer.
            //
            // Otherwise, we need to reuse some of the data in the current FFT
            // buffer for the overlap.
            if (fftoff + avail < Ni) {
                inoff += avail;
                fftoff += avail;

                f(I*fftoff/X - O);

                break;
            } else if (fftoff <= Li) {
                inoff += Li - fftoff;
                fftoff = 0;

                if (!f(L))
                    break;
            } else {
                std::copy(fft.in.begin() + Li,
                          fft.in.end(),
                          fft.in.begin());
                fftoff -= Li;

                if (!f(L))
                    break;
            }
        }

        return inoff;
    }

    /** @brief Return number of pending output samples in buffer */
    size_t npending(void)
    {
        size_t n = I*fftoff/X;

        return n > O ? n - O : 0;
    }

    /** @brief Save FFT offset
     * @return Current FFT offset
     */
    std::optional<size_t> saveFFTOffset() const
    {
        return fftoff;
    }

    /** @brief Restore FFT offset
     * @param fftoff_ New FFT offset
     */
    void restoreFFTOffset(size_t fftoff_)
    {
        fftoff = fftoff_;
    }

    /** @brief Copy most recent FFT output block
     * @param out Frequency domain buffer
     */
    void copyFFTOut(T* out)
    {
        upsampleBlock(fft.out.data(), out);
    }

    class ToTimeDomain
    {
    public:
        ToTimeDomain(void)
          : ifft(N, FFTW_BACKWARD)
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
    /** @brief Oversample factor */
    const unsigned X;

    /** @brief Interpolation factor */
    const unsigned I;

    /** @brief Number of bins to rotate */
    int Nrot;

    /** @brief FFT */
    fftw::FFT<T> fft;

    /** @brief Inverse FFT */
    fftw::FFT<T> ifft;

    /** @brief Offset into FFT input at which to place new data */
    size_t fftoff;
};

}

#endif /* FDUPSAMPLER_HH_ */
