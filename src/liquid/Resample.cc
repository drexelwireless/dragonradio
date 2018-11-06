#include <assert.h>

#include "liquid/Resample.hh"

namespace Liquid {

size_t MultiStageResampler::resample(const std::complex<float> *in, size_t count, std::complex<float> *out)
{
    unsigned nw;

    msresamp_crcf_execute(resamp_,
                          const_cast<std::complex<float>*>(in),
                          count,
                          out,
                          &nw);

    return nw;
}

}
