// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef POLYPHASE_H_
#define POLYPHASE_H_

#include <assert.h>

#include <vector>

#include <xsimd/xsimd.hpp>

#include "dsp/Filter.hh"
#include "dsp/Resample.hh"
#include "dsp/TableNCO.hh"
#include "dsp/Window.hh"

namespace dragonradio::signal::pfb {

using dragonradio::signal::Resampler;

/** @brief A polyphase filter bank
 * @tparam T The type of samples
 * @tparam C The type of filter taps
 */
template <class T, class C>
class Pfb : virtual public Resampler<T,T>
{
public:
    /** @brief Construct a polyphase filter bank
     * @param l The number of channels
     * @param taps The taps for the prototype FIR filter, which should have
     * unity gain.
     */
    Pfb(unsigned l, const std::vector<C> &taps)
      : l_(l)
      , taps_(taps)
      , adjtaps_(taps)
      , w_(1)
    {
        if (taps.size() == 0)
            throw std::range_error("Polyphase filter taps must be non-empty");

        reconfigure();
    }

    Pfb() = delete;

    virtual ~Pfb() = default;

    /** @brief Get the number of channels */
    unsigned getNumChannels(void) const
    {
        return l_;
    }

    /** @brief Set the number of channels */
    void setNumChannels(unsigned l)
    {
        l_ = l;
        reconfigure();
    }

    /** @brief Get prototype filter taps */
    const std::vector<C> &getTaps(void) const
    {
        return taps_;
    }

    /** @brief Set prototype filter taps */
    void setTaps(const std::vector<C> &taps)
    {
        if (taps.size() == 0)
            throw std::range_error("Polyphase filter taps must be non-empty");

        taps_ = taps;
        adjtaps_ = taps;
        reconfigure();
    }

    /** @brief Get (reversed) per-channel taps */
    std::vector<std::vector<C>> getChannelTaps(void)
    {
        std::vector<std::vector<C>> result(rtaps_.size());

        for (unsigned i = 0; i < rtaps_.size(); ++i) {
            result[i].resize(n_);
            std::copy(rtaps_[i].begin(), rtaps_[i].end(), result[i].begin());
        }

        return result;
    }

    double getDelay(void) const override
    {
        return (taps_.size() - 1.0)/2.0;
    }

protected:
    using taps_t = std::vector<C, xsimd::aligned_allocator<C>>;

    /** @brief Number of channels */
    unsigned l_;

    /** @brief Number of filter taps per channel */
    typename std::vector<C>::size_type n_;

    /** @brief Filter taps */
    std::vector<C> taps_;

    /** @brief Adjusted filter taps */
    std::vector<C> adjtaps_;

    /** @brief Per-channel filter taps, reversed */
    std::vector<taps_t> rtaps_;

    /** @brief Sample window */
    Window<T> w_;

    /** @brief Reconfigure filter bank for current number of channels and taps */
    virtual void reconfigure(void)
    {
        unsigned ntaps = adjtaps_.size();

        // Compute number of taps per channel. Each channel gets every mth tap
        // from the protoype filter, and we add additional 0 taps to ensure
        // every channel gets the same number of taps
        n_ = (ntaps + l_ - 1) / l_;
        assert(n_ > 0);

        // Resize sample window
        w_.resize(n_);

        // Resize per-channel reversed taps
        rtaps_.resize(l_);
        for (auto &rtaps : rtaps_) {
            // We need to pad the actual taps with zeroes so we can use SIMD
            // instructions
            rtaps.resize(n_ + xsimd::simd_type<C>::size - 1);
            std::fill(rtaps.begin(), rtaps.end(), 0);
        }

        // Fill per-channel reversed taps
        for (unsigned i = 0; i < ntaps; ++i)
            rtaps_[i % l_][n_ - 1 - i/l_] = static_cast<C>(l_)*adjtaps_[i];
    }
};

/** @brief An upsampler that uses a polyphase filter bank
 * @tparam T The type of samples
 * @tparam C The type of filter taps
 */
template <class T, class C>
class Upsampler : public Pfb<T,C> {
protected:
    using Pfb<T,C>::l_;
    using Pfb<T,C>::n_;
    using Pfb<T,C>::rtaps_;
    using Pfb<T,C>::w_;
    using Pfb<T,C>::reconfigure;

public:
    /** @brief Construct a polyphase upsampler
     * @param l The upsampling rate
     * @param taps The taps for the prototype FIR filter, which should have
     * unity gain.
     */
    Upsampler(unsigned l, const std::vector<C> &taps)
      : Pfb<T,C>(l, taps)
    {
        reconfigure();
    }

    Upsampler() = delete;

    virtual ~Upsampler() = default;

    double getRate(void) const override
    {
        return l_;
    }

    size_t neededOut(size_t count) const override
    {
        return count*l_;
    }

    void reset(void) override
    {
        w_.reset();
    }

    size_t resample(const T *in, size_t count, T *out) override final
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(in[i]);

            for (unsigned j = 0; j < l_; ++j)
                out[k++] = w_.dotprod(rtaps_[j].data(), xsimd::aligned_mode());
        }

        return k;
    }

protected:
    void reconfigure(void) override
    {
        Pfb<T,C>::reconfigure();
        reset();
    }
};

/** @brief A downsampler that uses a polyphase filter bank
 * @tparam T The type of samples
 * @tparam C The type of filter taps
 */
template <class T, class C>
class Downsampler : public Pfb<T,C> {
protected:
    using Pfb<T,C>::n_;
    using Pfb<T,C>::rtaps_;
    using Pfb<T,C>::w_;
    using Pfb<T,C>::reconfigure;

public:
    /** @brief Construct a downsampler
     * @param m The downsampling rate
     * @param taps The taps for the prototype FIR filter, which should have
     * unity gain.
     */
    Downsampler(unsigned m, const std::vector<C> &taps)
      : Pfb<T,C>(1, taps)
      , m_(m)
    {
        reconfigure();
    }

    Downsampler() = delete;

    virtual ~Downsampler() = default;

    double getRate(void) const override
    {
        return 1.0/m_;
    }

    size_t neededOut(size_t count) const override
    {
        return (count + idx_)/m_ + 1;
    }

    void reset(void) override
    {
        idx_ = 0;
        w_.reset();
    }

    size_t resample(const T *in, size_t count, T *out) override final
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(in[i]);

            if (idx_++ == 0)
                out[k++] = w_.dotprod(rtaps_[0].data(), xsimd::aligned_mode());

            idx_ %= m_;
        }

        return k;
    }

protected:
    /** @brief Downsampling rate */
    unsigned m_;

    /** @brief Input sample index */
    unsigned idx_;

    void reconfigure(void) override
    {
        Pfb<T,C>::reconfigure();
        reset();
    }
};

/** @brief A rational resampler that uses a polyphase filter bank
 * @tparam T The type of samples
 * @tparam C The type of filter taps
 */
template <class T, class C>
class RationalResampler : public Pfb<T,C>, public dragonradio::signal::RationalResampler<T,T> {
protected:
    using Pfb<T,C>::l_;
    using Pfb<T,C>::n_;
    using Pfb<T,C>::rtaps_;
    using Pfb<T,C>::w_;
    using Pfb<T,C>::reconfigure;

public:
    /** @brief Construct a polyphase rational resampler
     * @param l The interpolation rate
     * @param m The decimation rate
     * @param taps The taps for the prototype FIR filter
     */
    RationalResampler(unsigned l, unsigned m, const std::vector<C>& taps = {1.0})
      : Pfb<T,C>(l, taps)
      , m_(m)
    {
        reconfigure();
    }

    RationalResampler() = delete;

    virtual ~RationalResampler() = default;

    double getRate(void) const override
    {
        return static_cast<double>(l_)/static_cast<double>(m_);
    }

    unsigned getInterpolationRate(void) const override
    {
        return l_;
    }

    unsigned getDecimationRate(void) const override
    {
        return m_;
    }

    void setRate(unsigned l, unsigned m)
    {
        l_ = l;
        m_ = m;
        reconfigure();
    }

    size_t neededOut(size_t count) const override
    {
        return (count*l_ + idx_)/m_ + 1;
    }

    void reset(void) override
    {
        idx_ = 0;
        w_.reset();
    }

    size_t resample(const T *in, size_t count, T *out) override final
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(in[i]);

            for (unsigned j = 0; j < l_; ++j) {
                if (idx_++ == 0)
                    out[k++] = w_.dotprod(rtaps_[j].data(), xsimd::aligned_mode());

                idx_ %= m_;
            }
        }

        return k;
    }

protected:
    /** @brief Decimation rate */
    unsigned m_;

    /** @brief Upsampled input sample index */
    unsigned idx_;

    void reconfigure(void) override
    {
        Pfb<T,C>::reconfigure();
        reset();
    }
};

/** @brief A rational resampler that uses a polyphase filter bank and performs
 * mixing.
 */
template <class T, class C>
class MixingRationalResampler : public RationalResampler<T,C> {
protected:
    using RationalResampler<T,C>::l_;
    using RationalResampler<T,C>::m_;
    using RationalResampler<T,C>::w_;
    using RationalResampler<T,C>::idx_;
    using RationalResampler<T,C>::taps_;
    using RationalResampler<T,C>::rtaps_;
    using RationalResampler<T,C>::adjtaps_;

public:
    /** @brief Construct a polyphase rational resampler
     * @param l The interpolation rate
     * @param m The decimation rate
     * @param theta The frequency shift (normalized frequency)
     * @param taps The taps for the prototype FIR filter.
     */
    MixingRationalResampler(unsigned l, unsigned m, double theta, const std::vector<C> &taps)
      : RationalResampler<T,C>(l, m, taps)
      , theta_(theta)
      , nco_(0.0)
    {
        reconfigure();
    }

    MixingRationalResampler() = delete;

    virtual ~MixingRationalResampler() = default;

    /** @brief Get frequency shift (normalized frequency) */
    double getTheta(void)
    {
        return theta_;
    }

    /** @brief Set frequency shift (normalized frequency)
     * @param theta Frequency shift (normalized frequency).
     */
    /** The resampler will internally compensate for non-unity upsampler and
     * downsampler rates.
     */
    void setTheta(double theta)
    {
        theta_ = theta;
        reconfigure();
    }

    /** @brief Get mixed (bandpass) prototype filter taps */
    const std::vector<C> &getBandpassTaps(void) const
    {
        return adjtaps_;
    }

    void reset(void) override
    {
        RationalResampler<T,C>::reset();
        nco_.setPhase(0.0);
    }

    size_t resampleMixUp(const T *in, size_t count, T *out)
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(nco_.mix_up(in[i]));

            for (unsigned j = 0; j < l_; ++j) {
                if (idx_++ == 0)
                    out[k++] = w_.dotprod(rtaps_[j].data(), xsimd::aligned_mode());

                idx_ %= m_;
            }
        }

        return k;
    }

    template <typename S>
    size_t resampleMixUp(const T *in, size_t count, S scale, T *out)
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(nco_.mix_up(scale*in[i]));

            for (unsigned j = 0; j < l_; ++j) {
                if (idx_++ == 0)
                    out[k++] = w_.dotprod(rtaps_[j].data(), xsimd::aligned_mode());

                idx_ %= m_;
            }
        }

        return k;
    }

    size_t resampleMixDown(const T *in, size_t count, T *out)
    {
        size_t k = 0; // Output index

        for (unsigned i = 0; i < count; ++i) {
            w_.add(in[i]);

            for (unsigned j = 0; j < l_; ++j) {
                if (idx_++ == 0)
                    out[k++] = nco_.mix_down(w_.dotprod(rtaps_[j].data(), xsimd::aligned_mode()));

                idx_ %= m_;
            }
        }

        return k;
    }

protected:
    /** @brief Frequency shift (normalized frequency) */
    double theta_;

    /** @brief NCO used for mixing */
    TableNCO<> nco_;

    void reconfigure(void) override
    {
        double rate = RationalResampler<T,C>::getRate();

        // Our adjusted taps are the taps we get by transforming the prototype
        // lowpass filter into a bandpass filter. The frequency shift is
        // specified at the higher of the input and output rates, so we have to
        // compensate appropriately.
        TableNCO nco(rate > 1.0 ? 2*M_PI*theta_/static_cast<double>(m_)
                                : 2*M_PI*theta_/static_cast<double>(l_));

        adjtaps_.resize(taps_.size());

        for (unsigned i = 0; i < taps_.size(); ++i)
            adjtaps_[i] = nco.mix_up(taps_[i]);

        // Now that we have the proper adjusted taps, we can reconfigure the
        // base class.
        RationalResampler<T,C>::reconfigure();

        // And finally we reset the NCO
        if (rate > 1.0)
            nco_.reset(2*M_PI*theta_*static_cast<double>(l_)/static_cast<double>(m_));
        else
            nco_.reset(2*M_PI*theta_*static_cast<double>(m_)/static_cast<double>(l_));
    }
};

}

#endif /* POLYPHASE_H_ */
