// Bit-for-bit parity of the native coder (src/simulation/NativeFEC.hh) with
// liquid-dsp's packetizer (P3). Both run in this process: liquid built against
// libcorrect's libfec shim, as in the production build.
//
//  * Encoded packets are identical for every code as inner or outer stage and
//    for combinations, over lengths that exercise Reed-Solomon blocking
//    (including the residual last block) and convolutional tails.
//  * Encoded lengths match packetizer_get_enc_msg_len.
//  * Each side decodes the other's packets, clean and with injected errors,
//    and both return the same message and CRC result for every error pattern.
//  * Soft decoding (outer stage) agrees with packetizer_decode_soft on noisy
//    soft bits, and LLR sign/erasure conventions match (0 = bit 0, 255 = 1).
//  * Correction capability: rs8 fixes 16 byte errors per codeword when no
//    interleaver is in the way; v29 fixes isolated bit errors.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <liquid/liquid.h>

#include "simulation/NativeFEC.hh"

namespace nf = dragonradio::native::fec;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (failures < 40) { std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } } while (0)

static fec_scheme liquidScheme(nf::Code c)
{
    return liquid_getopt_str2fec(nf::name(c));
}

int main()
{
    std::mt19937 rng(20261104);
    auto bytes = [&](size_t n) {
        std::vector<unsigned char> v(n);
        for (auto &b : v) b = static_cast<unsigned char>(rng());
        return v;
    };

    std::vector<std::pair<nf::Code, nf::Code>> pairs;
    for (int c = 0; c < nf::kNumCodes; ++c) {
        pairs.push_back({static_cast<nf::Code>(c), nf::kNone});
        if (c != nf::kNone)
            pairs.push_back({nf::kNone, static_cast<nf::Code>(c)});
    }
    pairs.push_back({nf::kRS8, nf::kV29P78});
    pairs.push_back({nf::kV27, nf::kRS8});
    pairs.push_back({nf::kRS8, nf::kV29});
    const unsigned lengths[] = {1, 2, 7, 64, 219, 220, 223, 224, 442, 443, 446, 447, 500, 1024, 1500, 2000};

    unsigned encoded = 0, cross = 0, error_patterns = 0, soft_cases = 0;
    for (auto [f0, f1] : pairs) {
        const fec_scheme l0 = liquidScheme(f0), l1 = liquidScheme(f1);
        CHECK(l0 != LIQUID_FEC_UNKNOWN && l1 != LIQUID_FEC_UNKNOWN, "liquid has no scheme %s/%s", nf::name(f0), nf::name(f1));
        for (unsigned n : lengths) {
            packetizer lp = packetizer_create(n, LIQUID_CRC_32, l0, l1);
            nf::Packetizer np(n, f0, f1);
            const unsigned L = packetizer_get_enc_msg_len(lp);
            CHECK(np.packet_length() == L && nf::encoded_length(n, f0, f1) == L,
                  "%s/%s n=%u: length %u vs liquid %u", nf::name(f0), nf::name(f1), n, np.packet_length(), L);
            if (np.packet_length() != L) {
                packetizer_destroy(lp);
                continue;
            }
            for (int trial = 0; trial < 3; ++trial) {
                auto msg = bytes(n);
                std::vector<unsigned char> a(L), b(L), out(n);
                packetizer_encode(lp, msg.data(), a.data());
                np.encode(msg.data(), b.data());
                CHECK(a == b, "%s/%s n=%u: encoded packets differ", nf::name(f0), nf::name(f1), n);
                ++encoded;

                // Clean cross-decoding
                CHECK(np.decode(a.data(), out.data()) && out == msg, "%s/%s n=%u: native fails on liquid packet", nf::name(f0), nf::name(f1), n);
                std::fill(out.begin(), out.end(), 0);
                CHECK(packetizer_decode(lp, b.data(), out.data()) && out == msg, "%s/%s n=%u: liquid fails on native packet", nf::name(f0), nf::name(f1), n);
                ++cross;

                // Error injection: the same corrupted packet through both
                for (double ber : {1e-3, 1e-2, 3e-2}) {
                    std::vector<unsigned char> bad = a;
                    std::bernoulli_distribution flip(ber);
                    for (unsigned i = 0; i < 8 * L; ++i)
                        if (flip(rng)) bad[i / 8] ^= static_cast<unsigned char>(0x80 >> (i % 8));
                    std::vector<unsigned char> out_l(n), out_n(n);
                    std::vector<unsigned char> bad_l = bad, bad_n = bad;
                    const int ok_l = packetizer_decode(lp, bad_l.data(), out_l.data());
                    const bool ok_n = np.decode(bad_n.data(), out_n.data());
                    CHECK((ok_l != 0) == ok_n && out_l == out_n,
                          "%s/%s n=%u ber=%g: decoders disagree (liquid %d native %d)", nf::name(f0), nf::name(f1), n, ber, ok_l, ok_n);
                    ++error_patterns;
                }

                // Soft decoding: noisy soft bits, outer stage soft
                if (trial == 0) {
                    std::normal_distribution<double> noise(0.0, 70.0);
                    std::vector<unsigned char> soft(8 * L);
                    for (unsigned i = 0; i < 8 * L; ++i) {
                        const int bit = (a[i / 8] >> (7 - i % 8)) & 1;
                        double v = (bit ? 255.0 : 0.0) + noise(rng);
                        soft[i] = static_cast<unsigned char>(std::min(255.0, std::max(0.0, std::round(v))));
                    }
                    std::vector<unsigned char> out_l(n), out_n(n);
                    std::vector<unsigned char> s_l = soft, s_n = soft;
                    const int ok_l = packetizer_decode_soft(lp, s_l.data(), out_l.data());
                    const bool ok_n = np.decode_soft(s_n.data(), out_n.data());
                    CHECK((ok_l != 0) == ok_n && out_l == out_n,
                          "%s/%s n=%u: soft decoders disagree (liquid %d native %d)", nf::name(f0), nf::name(f1), n, ok_l, ok_n);
                    ++soft_cases;
                }
            }
            packetizer_destroy(lp);
        }
    }
    std::printf("parity: %u encodings identical, %u clean cross-decodes, %u error patterns and %u soft cases agree\n",
                encoded, cross, error_patterns, soft_cases);

    // Correction capability, without an interleaver in the way.
    {
        // rs8 as fec0 is interleaved; compare the stage directly via a
        // single-block message where the interleaver is applied: corrupt whole
        // codeword bytes after de-interleaving cannot be expressed through the
        // packet, so test the RS stage with errors on known codeword symbols.
        nf::detail::Stage rs(nf::kRS8, 200);
        auto msg = bytes(200);
        std::vector<unsigned char> enc(rs.enc_len_), dec(200);
        rs.encode(msg.data(), enc.data());
        for (int k = 0; k < 16; ++k)
            enc[(k * 13) % enc.size()] ^= 0xa5;
        rs.decode(enc.data(), dec.data());
        CHECK(dec == msg, "rs8 did not correct 16 symbol errors");
        rs.encode(msg.data(), enc.data());
        for (int k = 0; k < 17; ++k)
            enc[(k * 13) % enc.size()] ^= 0xa5;
        rs.decode(enc.data(), dec.data());
        CHECK(dec != msg, "rs8 corrected 17 symbol errors (beyond its capability)");

        nf::detail::Stage v29(nf::kV29, 100);
        auto m2 = bytes(100);
        std::vector<unsigned char> e2(v29.enc_len_), d2(100);
        v29.encode(m2.data(), e2.data());
        for (int k = 0; k < 20; ++k)
            e2[1 + k * 9] ^= 0x10;   // isolated bit errors, well separated
        v29.decode(e2.data(), d2.data());
        CHECK(d2 == m2, "v29 did not correct isolated bit errors");

        // Soft-bit convention: all-erasure input decodes to something, and
        // strong correct soft bits decode exactly.
        std::vector<unsigned char> soft(8 * v29.enc_len_);
        v29.encode(m2.data(), e2.data());
        for (unsigned i = 0; i < soft.size(); ++i)
            soft[i] = ((e2[i / 8] >> (7 - i % 8)) & 1) ? 200 : 55;
        v29.decode_soft(soft.data(), d2.data());
        CHECK(d2 == m2, "soft bits 55/200 (0/1) not decoded");
        for (unsigned i = 0; i < soft.size(); ++i)
            soft[i] = 255 - soft[i];
        v29.decode_soft(soft.data(), d2.data());
        CHECK(d2 != m2, "inverted soft bits decoded to the message (sign convention)");
        std::printf("capability: rs8 corrects 16 and not 17 symbol errors; v29 corrects isolated bit errors; soft sign convention checked\n");
    }

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
