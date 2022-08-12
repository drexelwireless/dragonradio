// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <catch2/catch.hpp>
#include <rapidcheck/catch.h>

#include "Math.hh"
#include "dsp/sintab.hh"

namespace rc {
rc::Gen<float> theta_gen = rc::gen::map<float>(rc::gen::arbitrary<float>(), [](float x) { return fmodf(x, 1e10); });
}

const float max_err = 1e-6f;

TEST_CASE("sintab") {
    sintab<> tab;

    rc::prop("from_brad . to_brad is identity",
        [&]() {
            float theta = *rc::theta_gen.as("theta");

            RC_CLASSIFY(fabsf(theta) < M_PI, "in (-pi, pi)");

            float theta_prime = tab.from_brad(tab.to_brad(theta));

            return fabsf(theta_prime - unwrapPhase(theta)) < max_err;
        });

    rc::prop("sin correct",
        [&]() {
            float theta = *rc::theta_gen.as("theta");

            RC_CLASSIFY(fabsf(theta) < M_PI, "in (-pi, pi)");

            return fabsf(tab.sin(theta) - sinf(theta)) < max_err;
        });

    rc::prop("cos correct",
        [&]() {
            float theta = *rc::theta_gen.as("theta");

            RC_CLASSIFY(fabsf(theta) < M_PI, "in (-pi, pi)");

            return fabsf(tab.cos(theta) - cosf(theta)) < max_err;
        });
}
