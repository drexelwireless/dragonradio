#ifndef TABLENCO_HH_
#define TABLENCO_HH_

#include "dsp/sintab.hh"
#include "dsp/NCO.hh"

using namespace std::complex_literals;

constexpr int INTBITS = 12;

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

    void reset(double dtheta) override final
    {
        theta_ = 0;
        dtheta_ = sintab<INTBITS>::to_brad(dtheta);
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

    sintab<INTBITS>::brad_t theta_;
    sintab<INTBITS>::brad_t dtheta_;
};

#endif /* TABLENCO_HH_ */
