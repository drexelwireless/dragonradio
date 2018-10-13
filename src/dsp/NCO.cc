#include <math.h>

#include "dsp/NCO.hh"

using namespace std::complex_literals;

static sintab<INTBITS> sintab_;

TableNCO::TableNCO(double dtheta)
{
    theta_ = 0;
    dtheta_ = sintab<INTBITS>::to_brad(dtheta);
}

TableNCO::~TableNCO()
{
}

void TableNCO::reset(double dtheta)
{
    theta_ = 0;
    dtheta_ = sintab<INTBITS>::to_brad(dtheta);
}

void TableNCO::mix_up(const std::complex<float> *in,
                      std::complex<float> *out,
                      size_t count)
{
    for (size_t i = 0; i < count; ++i, theta_ += dtheta_)
        out[i] = in[i] * (sintab_.cos(theta_) + 1if*sintab_.sin(theta_));
}

void TableNCO::mix_down(const std::complex<float> *in,
                        std::complex<float> *out,
                        size_t count)
{
    for (size_t i = 0; i < count; ++i, theta_ += dtheta_)
        out[i] = in[i] * (sintab_.cos(theta_) - 1if*sintab_.sin(theta_));
}
