#ifndef POLYPHASE_H_
#define POLYPHASE_H_

#include <assert.h>

#include <vector>

#include <xsimd/xsimd.hpp>

#include "Math.hh"
#include "dsp/Filter.hh"
#include "dsp/Resample.hh"
#include "dsp/Window.hh"

namespace Dragon {

/** @brief A polyphase filter bank */
template <class T, class C>
class Pfb
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
      , w_(1)
    {
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

protected:
    using taps_t = std::vector<C, XSIMD_DEFAULT_ALLOCATOR(C)>;

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
        // from the protype filter, and we add additional 0 taps to ensure every
        // channel gets the same number of taps
        n_ = (ntaps + l_ - 1) / l_;

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

/** @brief An upsampler that uses a polyphase filter bank */
template <class T, class C>
class Upsampler : public Pfb<T,C>, public Resampler<T,T> {
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

    double getDelay(void) const override
    {
        return (n_ + 1.0)/2.0;
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

/** @brief A downsampler that uses a polyphase filter bank */
template <class T, class C>
class Downsampler : public Pfb<T,C>, public Resampler<T,T> {
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

    double getDelay(void) const override
    {
        return (n_ + 1.0)/2.0;
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

/** @brief A rational resampler that uses a polyphase filter bank */
template <class T, class C>
class RationalResampler : public Pfb<T,C>, public Resampler<T,T> {
protected:
    using Pfb<T,C>::l_;
    using Pfb<T,C>::n_;
    using Pfb<T,C>::rtaps_;
    using Pfb<T,C>::w_;
    using Pfb<T,C>::reconfigure;

public:
    /** @brief Construct a polyphase rational resampler
     * @param l The upsampling rate
     * @param m The downsampling rate
     * @param taps The taps for the prototype FIR filter
     */
    RationalResampler(unsigned l, unsigned m, const std::vector<C> &taps)
      : Pfb<T,C>(l, taps)
      , m_(m)
    {
        reconfigure();
    }

    /** @brief Construct a polyphase rational resampler
     * @param r The resampler rate
     * @param taps The taps for the prototype FIR filter
     */
    RationalResampler(double r, const std::vector<C> &taps = {1.0})
      : Pfb<T,C>(1, taps)
      , m_(1)
    {
        long l, m;

        frap(r, 200, l, m);
        l_ = l;
        m_ = m;
        reconfigure();
    }

    RationalResampler() = delete;

    virtual ~RationalResampler() = default;

    double getRate(void) const override
    {
        return static_cast<double>(l_)/m_;
    }

    unsigned getUpRate(void) const
    {
        return l_;
    }

    unsigned getDownRate(void) const
    {
        return m_;
    }

    void setRate(unsigned l, unsigned m)
    {
        l_ = l;
        m_ = m;
        reconfigure();
    }

    void setRate(double rate)
    {
        long l, m;

        frap(rate, 200, l, m);
        l_ = l;
        m_ = m;
        reconfigure();
    }

    double getDelay(void) const override
    {
        return (n_ + 1.0)/2.0;
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
    /** @brief Downsampling rate */
    unsigned m_;

    /** @brief Upsampled input sample index */
    unsigned idx_;

    void reconfigure(void) override
    {
        Pfb<T,C>::reconfigure();
        reset();
    }
};

}

#endif /* POLYPHASE_H_ */
