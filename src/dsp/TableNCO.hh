#ifndef TABLENCO_HH_
#define TABLENCO_HH_

#include "dsp/NCO.hh"

constexpr int INTBITS = 12;

class TableNCO : public NCO
{
public:
    explicit TableNCO(double dtheta);
    TableNCO() = default;
    TableNCO(const TableNCO &) = default;
    TableNCO(TableNCO&&) = default;

    virtual ~TableNCO() = default;

    TableNCO& operator=(const TableNCO&) = default;
    TableNCO& operator=(TableNCO&&) = default;

    void reset(double dtheta) override final;

    void mix_up(const std::complex<float> *in,
                std::complex<float> *out,
                size_t count) override final;

    void mix_down(const std::complex<float> *in,
                  std::complex<float> *out,
                  size_t count) override final;

private:
    sintab<INTBITS>::brad_t theta_;
    sintab<INTBITS>::brad_t dtheta_;
};

#endif /* TABLENCO_HH_ */
