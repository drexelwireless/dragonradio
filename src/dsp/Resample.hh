// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef RESAMPLE_HH_
#define RESAMPLE_HH_

#include <complex>

#include "IQBuffer.hh"
#include "Math.hh"

namespace dragonradio::signal {

/** @brief Resample a signal
 * @tparam I Type of input signal values
 * @tparam O Type of output signal values
 */
template <class I, class O>
class Resampler {
public:
    Resampler() = default;
    virtual ~Resampler() = default;

    /** @brief Get resampling rate */
    virtual double getRate(void) const = 0;

    /** @brief Get group delay */
    virtual double getDelay(void) const = 0;

    /** @brief Compute number of samples in a resampled signal
     * @param count Number of samples in source signal
     * @return Number of samples in resampled signal
     */
    virtual size_t resampledSize(size_t count) const
    {
        return getRate()*count;
    }

    /** @brief Maximum number of output samples produced for given number input
     * samples
     * @param count Number of input samples
     * @return Number of output samples
     */
    virtual size_t neededOut(size_t count) const = 0;

    /** @brief Reset resampling state */
    virtual void reset(void) = 0;

    /** @brief Resample a signal
     * @param in Pointer to input samples
     * @param count Number of input samples
     * @param out Pointer to output samples
     * @return Number of output samples produced
     */
    virtual size_t resample(const I *in, size_t count, O *out) = 0;
};

using C = std::complex<float>;

template <>
class Resampler<C,C> {
public:
    Resampler() = default;
    virtual ~Resampler() = default;

    virtual double getRate(void) const = 0;

    virtual double getDelay(void) const = 0;

    virtual size_t resampledSize(size_t count) const
    {
        return getRate()*count;
    }

    virtual size_t neededOut(size_t count) const = 0;

    virtual void reset(void) = 0;

    virtual size_t resample(const C *in, size_t count, C *out) = 0;

    virtual std::shared_ptr<IQBuf> resample(const IQBuf &in)
    {
        auto     out = std::make_shared<IQBuf>(neededOut(in.size()));
        unsigned nw;

        nw = resample(in.data(), in.size(), out->data());
        assert(nw <= out->size());
        out->resize(nw);

        return out;
    }
};

/** @brief A rational resampler
 * @tparam I Type of input signal values
 * @tparam O Type of output signal values
 */
template <class I, class O>
class RationalResampler : virtual public Resampler<I,O> {
public:
    /** @brief Get interpolation rate */
    virtual unsigned getInterpolationRate(void) const = 0;

    /** @brief Get decimation rate */
    virtual unsigned getDecimationRate(void) const = 0;

    double getRate(void) const override
    {
        return static_cast<double>(getInterpolationRate())/static_cast<double>(getDecimationRate());
    }

    size_t resampledSize(size_t count) const override
    {
        return getInterpolationRate()*count/getDecimationRate();
    }
};

}
#endif /* RESAMPLE_HH_ */
