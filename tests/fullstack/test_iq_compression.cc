// FLAC IQ compression round trip (found 2026-11-11 with drgui on simulated
// logs). Samples are encoded as 14-bit integers; a sample at or beyond full
// scale (|x| >= 1) used to make the encoder drop the block containing it and
// every later block, so logged slots were silently truncated (to 0 samples if
// the first block overflowed). Samples are now saturated at full scale.
// Checks: every sample decodes; in-range samples are within one quantization
// step; out-of-range samples come back clipped to full scale.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "IQCompression.hh"

static int failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

static void roundtrip(const std::vector<fc32_t> &x, const char *what, float tol)
{
    buffer<char>   c = compressIQData(x.data(), x.size());
    buffer<fc32_t> y = decompressIQData(c.data(), c.size());
    bool ok = y.size() == x.size();

    for (size_t i = 0; ok && i < x.size(); ++i) {
        // Expected value: the input saturated to the 14-bit range.
        float re = std::fmax(std::fmin(x[i].real(), 8191.f/8192.f), -1.f);
        float im = std::fmax(std::fmin(x[i].imag(), 8191.f/8192.f), -1.f);

        if (std::fabs(y[i].real() - re) > tol || std::fabs(y[i].imag() - im) > tol)
            ok = false;
    }
    std::printf("     %s: %zu samples in, %zu out\n", what, x.size(), y.size());
    check(ok, what);
}

int main()
{
    const size_t           n = 40960;
    const float            tol = 2.f/8192.f;
    std::mt19937           rng(1);
    std::normal_distribution<float> g(0.f, 1.f);
    std::vector<fc32_t>    x(n);

    for (auto &v : x)
        v = fc32_t(0.003f*g(rng), 0.003f*g(rng));
    roundtrip(x, "noise well below full scale", tol);

    for (size_t at : {size_t(100), size_t(20000), size_t(39000)}) {
        auto y = x;
        y[at] = fc32_t(1.2f, -1.2f);
        char what[80];
        std::snprintf(what, sizeof(what), "one sample beyond full scale at %zu", at);
        roundtrip(y, what, tol);
    }

    auto big = x;
    for (auto &v : big)
        v *= 500.f;     // peaks around 6x full scale
    roundtrip(big, "every block beyond full scale", tol);

    std::printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
