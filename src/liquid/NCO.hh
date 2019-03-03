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

    BaseNCO(BaseNCO &&nco)
    {
        nco_ = nco.nco_;
        nco.nco_ = nullptr;
    }

    BaseNCO() = delete;
    BaseNCO(const BaseNCO&) = delete;

    virtual ~BaseNCO()
    {
        if (nco_)
            nco_crcf_destroy(nco_);
    }

    BaseNCO& operator=(BaseNCO &&nco)
    {
        nco_ = nco.nco_;
        nco.nco_ = nullptr;

        return *this;
    }

    BaseNCO& operator=(const BaseNCO&) = delete;

    void reset(double dtheta) override final
    {
        nco_crcf_set_phase(nco_, 0.0f);
        nco_crcf_set_frequency(nco_, dtheta);
    }

    double getFrequency(void) override final
    {
        return nco_crcf_get_frequency(nco_);
    }

    void setFrequency(double dtheta) override final
    {
        nco_crcf_set_frequency(nco_, dtheta);
    }

    double getPhase(void) override final
    {
        return nco_crcf_get_phase(nco_);
    }

    void setPhase(double theta) override final
    {
        nco_crcf_set_phase(nco_, theta);
    }

    std::complex<float> mix_up(const std::complex<float> in) override final
    {
        std::complex<float> out;

        nco_crcf_mix_up(nco_, in, &out);
        nco_crcf_step(nco_);
        return out;
    }

    std::complex<float> mix_down(const std::complex<float> in) override final
    {
        std::complex<float> out;

        nco_crcf_mix_down(nco_, in, &out);
        nco_crcf_step(nco_);
        return out;
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
    NCO(NCO &&) = default;

    virtual ~NCO()
    {
    }

    NCO& operator=(const NCO&) = delete;
    NCO& operator=(NCO&&) = default;
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
    VCO(VCO &&) = default;

    virtual ~VCO()
    {
    }

    VCO& operator=(const VCO&) = delete;
    VCO& operator=(VCO&&) = default;
};

}

#endif /* LIQUID_NCO_HH_ */
