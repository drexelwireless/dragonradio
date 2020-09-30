// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <assert.h>

#include "liquid/Filter.hh"

namespace Liquid {

std::vector<float> parks_mcclellan(unsigned int n,
                                   float        fc,
                                   float        As)
{
    std::vector<float> h(n);

    firdespm_lowpass(n, fc, As, 0.0f, h.data());

    return h;
}

std::vector<float> kaiser(unsigned int n,
                          float        fc,
                          float        As)
{
    std::vector<float> h(n);

    liquid_firdes_kaiser(n, fc, As, 0.0f, h.data());

    return h;
}

std::tuple<std::vector<float>, std::vector<float>>
butter_lowpass(unsigned int N,
               float fc,
               float f0,
               float Ap,
               float As)
{
    unsigned r = N % 2;     // odd/even order
    unsigned L = (N-r)/2;   // filter semi-length

    unsigned  h_len = 3*(L+r);
    std::vector<float> b(h_len);
    std::vector<float> a(h_len);

    liquid_iirdes(LIQUID_IIRDES_BUTTER, LIQUID_IIRDES_LOWPASS, LIQUID_IIRDES_SOS, N, fc, f0, Ap, As, b.data(), a.data());

    return std::make_tuple(a, b);
}

}
