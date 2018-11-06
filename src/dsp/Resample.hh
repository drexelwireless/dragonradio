#ifndef RESAMPLE_HH_
#define RESAMPLE_HH_

#include <complex>

#include "IQBuffer.hh"

class Resampler {
public:
    Resampler() = default;
    virtual ~Resampler() = default;

    virtual double getRate(void) const = 0;

    virtual double getDelay(void) const = 0;

    virtual size_t neededOut(size_t count) const = 0;

    virtual void reset(void) = 0;

    virtual size_t resample(const std::complex<float> *in,
                            size_t count,
                            std::complex<float> *out) = 0;

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
