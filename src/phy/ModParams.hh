#ifndef MODPARAMS_H_
#define MODPARAMS_H_

#include "dsp/NCO.hh"
#include "dsp/TableNCO.hh"
#include "liquid/Resample.hh"

/** @brief A class that bundles up resampler and mixing parameters */
class ModParams {
public:
    ModParams(const Liquid::ResamplerParams &params,
              double signal_rate_,
              double resamp_rate_,
              double shift_)
      : resamp(1.0,
               params.m,
               params.fc,
               params.As,
               params.npfb)
      , nco(0.0)
      , signal_rate(signal_rate_)
      , resamp_rate(1.0)
      , shift(0.0)
      , params_(params)
    {
        reconfigure(signal_rate_, resamp_rate_, shift_);
    }

    ModParams() = delete;

    ~ModParams() = default;

    void reconfigure(double signal_rate_,
                     double resamp_rate_,
                     double shift_)
    {
        if (resamp_rate_ != resamp_rate) {
            resamp = Liquid::MultiStageResampler(resamp_rate_,
                                                 params_.m,
                                                 params_.fc,
                                                 params_.As,
                                                 params_.npfb);
            resamp_rate = resamp.getRate();
        }

        if (shift_ != shift || signal_rate_ != signal_rate) {
            signal_rate = signal_rate_;
            shift = shift_;
            reconfigureNCO();
        }
    }

    void setFreqShift(double shift_)
    {
        if (shift_ != shift) {
            shift = shift_;
            reconfigureNCO();
        }
    }

    /** @brief Resampler */
    Liquid::MultiStageResampler resamp;

    /** @brief NCO for mixing */
    TableNCO nco;

    /** @brief Signal rate */
    double signal_rate;

    /** @brief Resampler rate */
    double resamp_rate;

    /** @brief  Frequency shift */
    double shift;

private:
    /** @brief Resampler parameters */
    const Liquid::ResamplerParams &params_;

    void reconfigureNCO(void)
    {
        double rad = 2*M_PI*shift/signal_rate; // Frequency shift in radians

        nco.reset(rad);
    }
};

#endif /* MODPARAMS_H_ */
