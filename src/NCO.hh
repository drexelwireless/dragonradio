#ifndef NCO_HH_
#define NCO_HH_

#include <sys/types.h>

#include <complex>
#include <memory>

#include <liquid/liquid.h>

class NCO {
public:
    NCO();
    virtual ~NCO();

    virtual void reset(double dtheta) = 0;

    virtual void mix_up(const std::complex<float> *in,
                        std::complex<float> *out,
                        size_t count) = 0;

    virtual void mix_down(const std::complex<float> *in,
                          std::complex<float> *out,
                          size_t count) = 0;
};

class LiquidNCO : public NCO
{
public:
    LiquidNCO(liquid_ncotype type, double dtheta);
    virtual ~LiquidNCO();

    LiquidNCO() = delete;
    LiquidNCO(const LiquidNCO&) = delete;
    LiquidNCO(LiquidNCO&&) = delete;

    LiquidNCO& operator=(const LiquidNCO&) = delete;
    LiquidNCO& operator=(LiquidNCO&&) = delete;

    void reset(double dtheta) override final;

    void mix_up(const std::complex<float> *in,
                std::complex<float> *out,
                size_t count) override final;

    void mix_down(const std::complex<float> *in,
                  std::complex<float> *out,
                  size_t count) override final;

private:
    nco_crcf nco_;
};

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
    uint32_t theta_;
    uint32_t dtheta_;
};

#endif /* NCO_HH_ */
