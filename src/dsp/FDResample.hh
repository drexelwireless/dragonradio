#ifndef FDRESAMPLE_HH_
#define FDRESAMPLE_HH_

#include <assert.h>

#include <complex>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "dsp/FFTW.hh"

template <typename T, unsigned P, unsigned V>
class FDUpsampler
{
public:
    FDUpsampler(unsigned X_, unsigned I_, int Nrot_)
      : X(X_)
      , I(I_)
      , Nrot(Nrot_)
      , fft(X*N/I, FFTW_FORWARD, FFTW_MEASURE)
    {
        const int n = N/I; // Size of input block, not counting oversampling

        if (Nrot > 0)
            assert(Nrot >= n/2);

        if (Nrot < 0)
            assert(Nrot <= n/2);

        reset();
    }

    FDUpsampler() = delete;

    void reset(size_t npartial = 0)
    {
        const unsigned Oi = X*O/I; // Overlap factor for input FFT

        fftoff = Oi + npartial;
        std::fill(fft.in.begin(), fft.in.begin() + fftoff, 0);
    }

    void upsampleBlock(T *out)
    {
        const unsigned Ni = X*N/I; // Size of forward FFT for input
        const int      n = N/I;    // Size of input block, not counting
                                   // oversampling
        auto           fftout = fft.out.begin();

        // Copy FFT buffer to out, upsampling and frequency shifting by rotating
        // bins. Note that we don't copy bins that result from oversampling on
        // the part of the modulator.
        //
        // Since N is always even, we need to split the bin at the Nyquist
        // frequency.
        if (Nrot == 0) {
            std::copy(fftout,
                      fftout + n/2,
                      out);
            std::copy(fftout + Ni - n/2 + 1,
                      fftout + Ni,
                      out + N - n/2 + 1);

            auto temp = fftout[n/2] / 2.0f;

            out[n/2] += temp;
            out[N - n/2] = temp;
        } else if (Nrot > 0) {
            std::copy(fftout,
                      fftout + n/2,
                      out + Nrot);
            std::copy(fftout + Ni - n/2 + 1,
                      fftout + Ni,
                      out + Nrot - n/2 + 1);

            auto temp = fftout[n/2] / 2.0f;

            out[Nrot + n/2] += temp;
            out[Nrot - n/2] = temp;
        } else {
            std::copy(fftout,
                      fftout + n/2,
                      out + N + Nrot);
            std::copy(fftout + Ni - n/2 + 1,
                      fftout + Ni,
                      out + N + Nrot - n/2 + 1);

            auto temp = fftout[n/2] / 2.0f;

            out[N + Nrot + n/2] += temp;
            out[N + Nrot - n/2] = temp;
        }
    }

    size_t upsample(const T *in,
                    size_t count,
                    T *out,
                    const float g,
                    bool flush,
                    size_t &nsamples,
                    size_t max_nsamples,
                    size_t &fdnsamples)
    {
        const unsigned Ni = X*N/I; // Size of forward FFT for input
        const unsigned Li = X*L/I; // Number of samples consumed per input block
        const unsigned Oi = X*O/I; // Overlap factor for input FFT
        size_t         inoff = 0;  // Offset into input buffer
        auto           fftin = fft.in.begin();

        // The upsampled signal is multiplied by this constant. It incorporates:
        //   * The requested gain
        //   * Scaling compensation for the FFT
        const float k = g/static_cast<float>(Ni);

        while (nsamples < max_nsamples) {
            size_t avail = count - inoff;

            if (fftoff + avail < Ni) {
                std::copy(in + inoff,
                          in + inoff + avail,
                          fftin + fftoff);

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
                          fftin + fftoff);
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
            upsampleBlock(out + fdnsamples);
            fdnsamples += N;

            // If the FFT buffer held up to Ni - Oi samples, we can get all the
            // data we need for the next FFT from the input buffer.
            //
            // Otherwise we need to reuse some of the data in the current FFT
            // buffer in the next round.
            if (fftoff + avail < Ni - Oi) {
                inoff += avail;
                fftoff += avail;
                nsamples += I*fftoff/X - O;

                break;
            } else if (fftoff <= Li) {
                inoff += Li - fftoff;
                fftoff = 0;
                nsamples += L;
            } else {
                std::copy(fft.in.begin() + Ni - 2*Oi,
                          fft.in.end(),
                          fft.in.begin());
                fftoff -= Li;
                nsamples += L;
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

    /** @brief Length of FFT */
    static constexpr unsigned N = V*(P-1);

    /** @brief Size of FFT overlap */
    static constexpr unsigned O = P-1;

    /** @brief Number of new samples consumed per input block */
    static constexpr unsigned L = N - 2*O;

    /** @brief Oversample factor */
    const unsigned X;

    /** @brief Interpolation factor */
    const unsigned I;

    /** @brief Number of bins to rotate */
    const int Nrot;

    /** @brief FFT */
    fftw::FFT<T> fft;

    /** @brief Offset into FFT input at which to place new data */
    size_t fftoff;

    class ToTimeDomain
    {
    public:
        ToTimeDomain(void)
          : ifft(N, FFTW_BACKWARD, FFTW_MEASURE)
        {
        }

        size_t toTimeDomain(const T *in, size_t count, T *out)
        {
            size_t outoff = 0;

            for (; count >= N; count -= N, in += N) {
                // Copy data into IFFT buffer
                std::copy(in, in + N, ifft.in.begin());

                // Perform IFFT
                ifft.execute();

                // Copy time-domain data into IQ buffer
                std::copy(ifft.out.begin() + O,
                          ifft.out.end() - O,
                          out + outoff);
                outoff += L;
            }

            return outoff;
        }

        /** @brief FFT */
        fftw::FFT<T> ifft;
    };

};

#endif /* FDRESAMPLE_HH_ */
