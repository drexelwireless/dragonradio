#include <math.h>

#include "NCO.hh"

using namespace std::complex_literals;

// These constants determine the number of bits we use to represent numbers in
// the range [0, 2*pi)
const int INTBITS = 12;
const int FRACBITS = 32 - INTBITS;

// This is the size of our table
const int N = 1 << INTBITS;

// Fixed-point representation of pi/2
const uint32_t PIDIV2 = 1 << 30;

class sintab {
public:
    sintab()
    {
        for (unsigned i = 0; i < N; ++i)
            sintab_[i] = sin(2.0*M_PI*static_cast<double>(i)/static_cast<double>(N));
    }

    ~sintab()
    {
    }

    float operator [](uint32_t pos)
    {
        return sintab_[pos >> FRACBITS];
    }

private:
    float sintab_[N];
};

static sintab sintab_;

TableNCO::TableNCO(double dtheta)
{
    theta_ = 0;
    dtheta_ = dtheta*(static_cast<double>(N)/(2.0*M_PI) * (1 << FRACBITS));
}

TableNCO::~TableNCO()
{
}

void TableNCO::reset(double dtheta)
{
    theta_ = 0;
    dtheta_ = dtheta*(static_cast<double>(N)/(2.0*M_PI) * (1 << FRACBITS));
}

void TableNCO::mix_up(const std::complex<float> *in,
                      std::complex<float> *out,
                      size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        float sine = sintab_[theta_];
        float cosine = sintab_[theta_ + PIDIV2];

        out[i] = in[i] * (cosine + 1if*sine);

        theta_ += dtheta_;
    }
}

void TableNCO::mix_down(const std::complex<float> *in,
                        std::complex<float> *out,
                        size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        float sine = sintab_[theta_];
        float cosine = sintab_[theta_ + PIDIV2];

        out[i] = in[i] * (cosine - 1if*sine);

        theta_ += dtheta_;
    }
}
