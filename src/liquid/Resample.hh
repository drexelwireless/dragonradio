#ifndef LIQUID_RESAMPLE_HH_
#define LIQUID_RESAMPLE_HH_

#include <complex>
#include <memory>

#include <liquid/liquid.h>

#include "IQBuffer.hh"
#include "dsp/Resample.hh"

namespace Liquid {

class MultiStageResampler : public Resampler {
public:
   /** @brief Create a liquid multi-stage resampler
    * @param rate Resampling rate
    * @param m Prototype filter semi-length
    * @param fc Prototype filter cutoff frequency, in range (0, 0.5)
    * @param As Stop-band attenuation
    * @param npfb Number of filters in polyphase filterbank
    */
    MultiStageResampler(float rate,
                        unsigned m,
                        float fc,
                        float As,
                        unsigned npfb)
    {
        resamp_ = msresamp_crcf_create(rate, m, fc, As, npfb);
        rate_ = msresamp_crcf_get_rate(resamp_);
        delay_ = msresamp_crcf_get_delay(resamp_);
    }

    MultiStageResampler(MultiStageResampler &&resamp)
    {
        msresamp_crcf_destroy(resamp_);
        resamp_ = resamp.resamp_;
        resamp.resamp_ = nullptr;

        rate_ = resamp.rate_;
        delay_ = resamp.delay_;
    }

    virtual ~MultiStageResampler()
    {
        if (resamp_)
            msresamp_crcf_destroy(resamp_);
    }

    MultiStageResampler &operator =(MultiStageResampler &&resamp)
    {
        msresamp_crcf_destroy(resamp_);
        resamp_ = resamp.resamp_;
        resamp.resamp_ = nullptr;

        rate_ = resamp.rate_;
        delay_ = resamp.delay_;

        return *this;
    }

    MultiStageResampler& operator=(const MultiStageResampler&) = delete;

    double getRate(void) const override final
    {
        return rate_;
    }

    double getDelay(void) const override final
    {
        return delay_;
    }

    size_t neededOut(size_t count) const override final
    {
        return 1 + 2*rate_*count;
    }

    virtual void reset(void) override final
    {
        return msresamp_crcf_reset(resamp_);
    }

    virtual size_t resample(const std::complex<float> *in, size_t count, std::complex<float> *out) override final;

    using Resampler::resample;

    void print(void)
    {
        return msresamp_crcf_print(resamp_);
    }

protected:
    msresamp_crcf resamp_;
    double rate_;
    double delay_;
};

}

#endif /* LIQUID_RESAMPLE_HH_ */
