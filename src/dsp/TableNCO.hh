// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TABLENCO_HH_
#define TABLENCO_HH_

#include "dsp/sintab.hh"
#include "dsp/NCO.hh"

using namespace std::complex_literals;

template<int INTBITS = 12>
class TableNCO : public NCO
{
public:
    explicit TableNCO(double dtheta)
    {
        theta_ = 0;
        dtheta_ = sintab<INTBITS>::to_brad(dtheta);
    }

    TableNCO() = default;
    TableNCO(const TableNCO &) = default;
    TableNCO(TableNCO&&) = default;

    virtual ~TableNCO() = default;

    TableNCO& operator=(const TableNCO&) = default;
    TableNCO& operator=(TableNCO&&) = default;

    double getFrequency(void) override final
    {
        return sintab<INTBITS>::from_brad(dtheta_);
    }

    void setFrequency(double dtheta) override final
    {
        dtheta_ = sintab<INTBITS>::to_brad(dtheta);
    }

    double getPhase(void) override final
    {
        return theta_;
    }

    void setPhase(double theta) override final
    {
        theta_ = theta;
    }

    void reset(double dtheta) override final
    {
        theta_ = 0;
        dtheta_ = sintab<INTBITS>::to_brad(dtheta);
    }

    std::complex<float> mix_up(const std::complex<float> in) override final
    {
        std::complex<float> out;

        out = in * (sintab_.cos(theta_) + 1if*sintab_.sin(theta_));
        theta_ += dtheta_;
        return out;
    }

    std::complex<float> mix_down(const std::complex<float> in) override final
    {
        std::complex<float> out;

        out = in * (sintab_.cos(theta_) - 1if*sintab_.sin(theta_));
        theta_ += dtheta_;
        return out;
    }

    void mix_up(const std::complex<float> *in,
                std::complex<float> *out,
                size_t count) override final
    {
        for (size_t i = 0; i < count; ++i, theta_ += dtheta_)
            out[i] = in[i] * (sintab_.cos(theta_) + 1if*sintab_.sin(theta_));
    }

    void mix_down(const std::complex<float> *in,
                  std::complex<float> *out,
                  size_t count) override final
    {
        for (size_t i = 0; i < count; ++i, theta_ += dtheta_)
            out[i] = in[i] * (sintab_.cos(theta_) - 1if*sintab_.sin(theta_));
    }

private:
    static sintab<INTBITS> sintab_;

    typename sintab<INTBITS>::brad_t theta_;
    typename sintab<INTBITS>::brad_t dtheta_;
};

template<int INTBITS>
sintab<INTBITS> TableNCO<INTBITS>::sintab_;

extern template
sintab<> TableNCO<>::sintab_;

#endif /* TABLENCO_HH_ */
