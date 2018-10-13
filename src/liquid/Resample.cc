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

std::shared_ptr<IQBuf> MultiStageResampler::resample(IQBuf &in)
{
    auto     out = std::make_shared<IQBuf>(1 + 2*rate_*in.size());
    unsigned nw;

    nw = resample(in.data(), in.size(), out->data());
    assert(nw <= out->size());
    out->resize(nw);

    return out;
}

}
