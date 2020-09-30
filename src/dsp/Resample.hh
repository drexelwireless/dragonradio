// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef RESAMPLE_HH_
#define RESAMPLE_HH_

#include <complex>

#include "IQBuffer.hh"

template <class I, class O>
class Resampler {
public:
    Resampler() = default;
    virtual ~Resampler() = default;

    virtual double getRate(void) const = 0;

    virtual double getDelay(void) const = 0;

    virtual size_t neededOut(size_t count) const = 0;

    virtual void reset(void) = 0;

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

#endif /* RESAMPLE_HH_ */
