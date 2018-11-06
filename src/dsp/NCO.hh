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

#endif /* NCO_HH_ */
