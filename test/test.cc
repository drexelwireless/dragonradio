#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch.hpp>
#include <rapidcheck/catch.h>

#include <vector>

#include "IQCompression.hh"

namespace rc {
template<>
struct Arbitrary<IQBuf> {
    static Gen<IQBuf> arbitrary()
    {
        auto gen_mag = gen::map<int>(gen::inRange(-32767, 32768), [](int x) { return x/32768.0f; });
        auto gen_phase = gen::map<int>(gen::inRange(-32767, 32768), [](int x) { return (x*M_PI)/32768.0f; });

        return gen::container<IQBuf>(
            gen::apply([](float mag, float phase) { return std::polar<float>(mag, phase); },
            gen_mag,
            gen_phase));
    };
};
}

inline void showValue(const IQBuf &value, std::ostream &os)
{
    rc::showCollection("[", "]", value, os);
}

TEST_CASE("IQ data compression") {
    rc::prop("decompressIQData . compressIQData is identity",
        []() {
            auto sz = *rc::gen::inRange(0, 100000).as("size");
            IQBuf iq = *rc::gen::resize(sz, (rc::gen::arbitrary<IQBuf>())).as("IQ data");
            auto compressed = compressIQData(iq.data(), iq.size());
            auto decompressed = decompressIQData(compressed.data(), compressed.size());

            RC_CLASSIFY(iq.size() == 0,                           "empty");
            RC_CLASSIFY(iq.size() > 100   && iq.size() <= 1000,   "> 100");
            RC_CLASSIFY(iq.size() > 1000  && iq.size() <= 10000,  "> 1000");
            RC_CLASSIFY(iq.size() > 10000,                        "> 10000");

            RC_ASSERT(decompressed.size() == iq.size());

            float max_diff = 0;

            for (size_t i = 0; i < iq.size(); ++i)
            max_diff = std::max(max_diff, std::abs(iq[i] - decompressed[i]));

            return max_diff < 1e-3;
        });
}
