// Equivalence checks for the P4 native hot-path optimizations
// (src/simulation/NativeFrame.hh, FastPHYCore.hh). Each optimization must
// leave decoded results unchanged:
//
//  * soft_bits (threshold table) equals the per-bit exp/lround formula it
//    replaced (soft_bits_reference) for both decisions, over random LLRs,
//    LLRs just either side of every rounding step, zero and saturation.
//  * Hard decoding equals soft decoding for every code pair whose outer
//    stage is not convolutional (liquid's soft decoder hard-decides those
//    soft bits), on random corrupted packets: same message, same CRC result.
//  * all_finite flags any NaN or infinity at any position, and the stream
//    still rejects non-finite input.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "simulation/NativeFrame.hh"

namespace nv = dragonradio::native;
namespace nf = dragonradio::native::fec;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (failures < 40) { std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } } while (0)

static void soft_table()
{
    std::mt19937_64 rng(4);
    std::uniform_real_distribution<double> u(0.0, 12.0);
    std::vector<double> llrs = {0.0, 1e-300, 1e-17, 1e-12, 60.0, 61.0, 1e6};
    for (int i = 0; i < 2000000; ++i) llrs.push_back(u(rng));
    // Either side of every step of lround(255 p).
    for (int j = 128; j < 255; ++j) {
        const double q = (j + 0.5) / 255, t = std::log(q / (1 - q));
        for (double d : {-1e-9, -1e-12, 1e-12, 1e-9})
            llrs.push_back(t + d);
    }
    unsigned mismatches = 0;
    std::vector<unsigned char> decided(1), out(1);
    std::vector<float> conf(1);
    for (double llr : llrs) {
        for (unsigned char d : {0, 1}) {
            // soft_bits computes llr = 4 * half_dmin * confidence / n0; use
            // half_dmin = 1/4, n0 = 1 so that llr equals the confidence.
            conf[0] = static_cast<float>(llr);
            decided[0] = d;
            nv::FramedPHY::soft_bits(decided.data(), conf.data(), 1, 1.0, 0.25, out.data());
            const unsigned char ref = nv::FramedPHY::soft_bits_reference(d, double(conf[0]));
            if (out[0] != ref) {
                if (++mismatches < 5)
                    std::fprintf(stderr, "llr %.17g decided %d: table %d reference %d\n", double(conf[0]), d, out[0], ref);
            }
        }
    }
    CHECK(mismatches == 0, "soft_bits table differs from the reference in %u of %zu cases", mismatches, 2 * llrs.size());
    std::printf("ok   soft_bits table equals exp/lround reference (%zu cases)\n", 2 * llrs.size());
}

static void hard_equals_soft()
{
    std::mt19937_64 rng(5);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    // Outer (fec1) not convolutional: none or rs8; inner anything.
    const std::vector<std::pair<nf::Code, nf::Code>> pairs = {
        {nf::kRS8, nf::kNone}, {nf::kNone, nf::kRS8}, {nf::kV27, nf::kNone}, {nf::kV29P78, nf::kNone},
        {nf::kV27, nf::kRS8}, {nf::kV29, nf::kRS8}, {nf::kRS8, nf::kRS8}};
    unsigned cases = 0, valid = 0;
    for (auto [f0, f1] : pairs) {
        for (unsigned n : {1u, 64u, 223u, 500u, 1004u}) {
            nf::Packetizer pk(n, f0, f1);
            const unsigned len = pk.packet_length();
            for (int trial = 0; trial < 60; ++trial) {
                std::vector<unsigned char> msg(n), enc(len);
                for (auto &b : msg) b = static_cast<unsigned char>(rng());
                pk.encode(msg.data(), enc.data());
                // BPSK-like observation of every bit at an SNR that leaves a
                // few errors (more as trial grows), then decisions and
                // confidences as the demapper produces them.
                const float sigma = 0.25f + 0.01f * trial;
                std::vector<unsigned char> hard(len, 0), decided(8 * len), soft(8 * len);
                std::vector<float> conf(8 * len);
                for (unsigned i = 0; i < 8 * len; ++i) {
                    const int bit = (enc[i / 8] >> (7 - i % 8)) & 1;
                    float y = (bit ? -1.0f : 1.0f) + sigma * noise(rng);
                    if (y == 0.0f) y = 1e-6f;   // an exact tie is the one documented difference
                    decided[i] = y < 0;
                    conf[i] = std::abs(y);
                    hard[i / 8] |= static_cast<unsigned char>(decided[i] << (7 - i % 8));
                }
                nv::FramedPHY::soft_bits(decided.data(), conf.data(), 8 * len, sigma * sigma, 0.5, soft.data());
                std::vector<unsigned char> out_hard(n), out_soft(n);
                const bool ok_hard = pk.decode(hard.data(), out_hard.data());
                const bool ok_soft = pk.decode_soft(soft.data(), out_soft.data());
                CHECK(ok_hard == ok_soft && out_hard == out_soft,
                      "%s/%s n=%u trial %d: hard (crc %d) and soft (crc %d) decoding differ",
                      nf::name(f0), nf::name(f1), n, trial, ok_hard, ok_soft);
                ++cases;
                valid += ok_hard;
            }
        }
    }
    std::printf("ok   hard decoding equals soft decoding for non-convolutional outer codes (%u cases, %u valid)\n", cases, valid);
}

static void finite()
{
    using C = nv::C;
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    for (int count : {1, 7, 64, 1001}) {
        std::vector<C> x(count, C(0.5f, -0.25f));
        CHECK(nv::all_finite(x.data(), count), "finite input of %d flagged", count);
        for (float b : bad)
            for (int pos : {0, count / 2, count - 1})
                for (int part = 0; part < 2; ++part) {
                    std::vector<C> y(x);
                    y[pos] = part ? C(0, b) : C(b, 0);
                    CHECK(!nv::all_finite(y.data(), count), "non-finite at %d (part %d) of %d not flagged", pos, part, count);
                }
    }
    std::vector<C> big(5, C(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));
    CHECK(nv::all_finite(big.data(), 5), "largest finite values flagged");
    // The framed stream still rejects non-finite IQ before changing state.
    std::vector<int> data, pilots;
    for (int k = 1; k <= 20; ++k) (k % 5 ? data : pilots).push_back(k);
    nv::FramedPHY phy(32, 4, data, pilots, 64, 0);
    nv::FramedStream stream(phy);
    std::vector<nv::FramedStream::Result> results;
    std::vector<C> in(100, C(0.1f, 0.1f));
    in[57] = C(std::numeric_limits<float>::quiet_NaN(), 0);
    bool threw = false;
    try { stream.feed(in.data(), int(in.size()), false, results); } catch (const std::runtime_error &) { threw = true; }
    CHECK(threw, "stream accepted NaN");
    CHECK(stream.buffer.empty(), "stream kept samples from rejected input");
    std::printf("ok   all_finite and stream rejection of non-finite IQ\n");
}

int main()
{
    soft_table();
    hard_equals_soft();
    finite();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
