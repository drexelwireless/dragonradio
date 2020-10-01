// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <assert.h>

#include "liquid/Resample.hh"

namespace liquid {

size_t MultiStageResampler<C,C,F>::resample(const C *in, size_t count, C *out)
{
    unsigned nw;

    msresamp_crcf_execute(resamp_,
                          const_cast<C*>(in),
                          count,
                          out,
                          &nw);

    return nw;
}

}
