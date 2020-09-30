#ifndef FIR_H_
#define FIR_H_

#include <vector>

#include <xsimd/xsimd.hpp>

#include "dsp/Filter.hh"
#include "dsp/Window.hh"

#if defined(DOXYGEN)
#define final
#endif /* defined(DOXYGEN) */

namespace Dragon {

template <class T, class C>
class FIR : public ::FIR<T,T,C>
{
public:
    FIR(const std::vector<C> &taps)
      : w_(taps.size())
    {
        setTaps(taps);
    }

    FIR() = delete;

    virtual ~FIR() = default;

    virtual float getGroupDelay(float fc) const override final
    {
        return delay_;
    }

    virtual void reset(void) override final
    {
        w_.reset();
    }

    virtual void execute(const T *x, T *y, size_t n) override final
    {
        for (size_t i = 0; i < n; ++i) {
            in(x[i]);
            y[i] = out();
        }
    }

    virtual float getDelay(void) const override final
    {
        return delay_;
    }

    virtual const std::vector<C> &getTaps(void) const override final
    {
        return taps_;
    }

    virtual void setTaps(const std::vector<C> &taps) override final
    {
        n_ = taps.size();

        w_.resize(n_);
        w_.reset();

        taps_ = taps;

        // Make a reversed and simd-aligned copy of the taps suitable for a dot
        // product computation.
        rtaps_.resize(n_ + xsimd::simd_type<T>::size - 1);

        for (unsigned i = 0; i < n_; ++i)
            rtaps_[i] = taps[taps.size() - 1 - i];

        std::fill(rtaps_.begin() + n_, rtaps_.end(), 0);

        delay_ = (n_ - 1.0) / 2.0;
    }

protected:
    /** @brief Window size/number of filter taps */
    typename std::vector<C>::size_type n_;

    /** @brief Sample window */
    Window<T> w_;

    /** @brief Filter taps */
    std::vector<C> taps_;

    /** @brief Filter taps, reversed */
    std::vector<C, XSIMD_DEFAULT_ALLOCATOR(C)> rtaps_;

    /** @brief Delay */
    float delay_;

    /** @brief Add a sample to the FIR window */
    void in(T x)
    {
        w_.add(x);
    }

    /** @brief Compute FIR output using current window */
    T out(void)
    {
        return w_.dotprod(rtaps_.data(), xsimd::aligned_mode());
    }
};

}

#endif /* FIR_H_ */
