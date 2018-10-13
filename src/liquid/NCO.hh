#ifndef LIQUID_NCO_HH_
#define LIQUID_NCO_HH_

#include <complex>

#include <liquid/liquid.h>

#include "dsp/NCO.hh"

namespace Liquid {

class BaseNCO : public ::NCO
{
public:
    BaseNCO(liquid_ncotype type, double dtheta)
    {
        nco_ = nco_crcf_create(type);
        nco_crcf_set_phase(nco_, 0.0f);
        nco_crcf_set_frequency(nco_, dtheta);
    }

    BaseNCO() = delete;
    BaseNCO(const BaseNCO&) = delete;
    BaseNCO(BaseNCO&&) = delete;

    virtual ~BaseNCO()
    {
        nco_crcf_destroy(nco_);
    }

    BaseNCO& operator=(const BaseNCO&) = delete;
    BaseNCO& operator=(BaseNCO&&) = delete;

    void reset(double dtheta) override final
    {
        nco_crcf_set_phase(nco_, 0.0f);
        nco_crcf_set_frequency(nco_, dtheta);
    }

    void mix_up(const std::complex<float> *in,
                std::complex<float> *out,
                size_t count) override final
    {
        nco_crcf_mix_block_up(nco_, const_cast<std::complex<float>*>(in), out, count);
    }

    void mix_down(const std::complex<float> *in,
                  std::complex<float> *out,
                  size_t count) override final
    {
        nco_crcf_mix_block_down(nco_, const_cast<std::complex<float>*>(in), out, count);
    }

private:
    nco_crcf nco_;
};

/** @brief A numerically-controlled oscillator (fast) */
class NCO : public BaseNCO
{
public:
    NCO(double dtheta)
      : BaseNCO(LIQUID_NCO, dtheta)
    {
    }

    NCO() = delete;
    NCO(const NCO&) = delete;
    NCO(NCO&&) = delete;

    virtual ~NCO()
    {
    }

    NCO& operator=(const NCO&) = delete;
    NCO& operator=(NCO&&) = delete;
};

/** @brief A "voltage"-controlled oscillator (precise) */
class VCO : public BaseNCO
{
public:
    VCO(double dtheta)
      : BaseNCO(LIQUID_VCO, dtheta)
    {
    }

    VCO() = delete;
    VCO(const VCO&) = delete;
    VCO(VCO&&) = delete;

    virtual ~VCO()
    {
    }

    VCO& operator=(const VCO&) = delete;
    VCO& operator=(VCO&&) = delete;
};

}

#endif /* LIQUID_NCO_HH_ */
