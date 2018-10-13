#ifndef NCO_HH_
#define NCO_HH_

#include <sys/types.h>

#include <complex>
#include <memory>

#include <liquid/liquid.h>

#include "sintab.hh"

class NCO {
public:
    NCO() = default;
    virtual ~NCO() = default;

    virtual void reset(double dtheta) = 0;

    virtual void mix_up(const std::complex<float> *in,
                        std::complex<float> *out,
                        size_t count) = 0;

    virtual void mix_down(const std::complex<float> *in,
                          std::complex<float> *out,
                          size_t count) = 0;
};

constexpr int INTBITS = 12;

class TableNCO : public NCO
{
public:
    TableNCO(double dtheta);
    virtual ~TableNCO();

    TableNCO() = delete;
    TableNCO(const TableNCO&) = delete;
    TableNCO(TableNCO&&) = delete;

    TableNCO& operator=(const TableNCO&) = delete;
    TableNCO& operator=(TableNCO&&) = delete;

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

#endif /* NCO_HH_ */
