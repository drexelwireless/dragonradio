// Native Gray rectangular QAM, 1..8 bits per tone (P4 item 2.5;
// src/simulation/NativeFrame.hh Constellation):
//  * unit average power; Gray labels (axis neighbours differ in one bit);
//  * map/demap round trip for every point, with noise below half the
//    minimum distance;
//  * QPSK and 16-QAM are exactly the original mappings, and decisions match
//    the original demapper (FastPHY::demap, still used for them), so their
//    wire format and decoding are unchanged;
//  * max-log reliabilities: zero at a decision boundary, equal to the exact
//    max-log metric;
//  * FramedPHY frames for every order decode with valid CRC through noise.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "simulation/NativeFrame.hh"

namespace nv = dragonradio::native;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (failures < 40) { std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } } while (0)

int main()
{
    std::mt19937 rng(11);
    for (int b = 1; b <= 8; ++b) {
        const auto &c = nv::Constellation::get(b);
        const int n = 1 << b;
        double power = 0;
        std::vector<nv::C> pts(n);
        for (int v = 0; v < n; ++v) {
            int bits[8];
            for (int k = 0; k < b; ++k) bits[k] = (v >> (b - 1 - k)) & 1;
            pts[v] = c.map(bits);
            power += std::norm(pts[v]);
        }
        power /= n;
        CHECK(std::abs(power - 1) < 1e-5, "%d bits: average power %g", b, power);
        // Distinct points; neighbours at minimum distance differ in one bit.
        const double dmin = 2 * c.unit;
        unsigned neighbours = 0;
        for (int u = 0; u < n; ++u)
            for (int v = u + 1; v < n; ++v) {
                const double d = std::abs(pts[u] - pts[v]);
                CHECK(d > 0.99 * dmin, "%d bits: points %d %d closer than dmin", b, u, v);
                if (d < 1.01 * dmin) {
                    ++neighbours;
                    CHECK(__builtin_popcount(u ^ v) == 1, "%d bits: neighbours %d %d differ in %d bits", b, u, v, __builtin_popcount(u ^ v));
                }
            }
        CHECK(neighbours > 0, "%d bits: no neighbours", b);
        // Round trip with noise inside the decision region.
        std::uniform_real_distribution<float> u(-0.9f * c.unit, 0.9f * c.unit);
        for (int v = 0; v < n; ++v)
            for (int t = 0; t < 20; ++t) {
                const nv::C z = pts[v] + nv::C(u(rng), b > 1 ? u(rng) : 0.f);
                int hard[8];
                float conf[8];
                c.demap(z, 1.0f, hard, conf);
                int got = 0;
                for (int k = 0; k < b; ++k) { got = (got << 1) | hard[k]; CHECK(conf[k] > 0, "%d bits: confidence %g", b, conf[k]); }
                CHECK(got == v, "%d bits: point %d decided as %d", b, v, got);
            }
        // Max-log metric: on a boundary between two neighbours the differing bit has zero confidence.
        for (int u0 = 0; u0 < n; ++u0)
            for (int v = 0; v < n; ++v)
                if (__builtin_popcount(u0 ^ v) == 1 && std::abs(pts[u0] - pts[v]) < 1.01 * dmin) {
                    int hard[8];
                    float conf[8];
                    c.demap(0.5f * (pts[u0] + pts[v]), 1.0f, hard, conf);
                    const int k = b - 1 - __builtin_ctz(u0 ^ v);
                    CHECK(std::abs(conf[k]) < 1e-5, "%d bits: boundary confidence %g", b, conf[k]);
                }
    }
    // QPSK and 16-QAM: identical to the original mapping and FastPHY::demap.
    for (int b : {2, 4}) {
        const auto &c = nv::Constellation::get(b);
        CHECK(c.unit == (b == 2 ? nv::Q : nv::A16), "%d bits: unit %.9g", b, c.unit);
        std::normal_distribution<float> g(0, 0.4f);
        for (int t = 0; t < 20000; ++t) {
            const nv::C z(g(rng), g(rng));
            int h1[8], h2[8];
            float c1[8], c2[8];
            const float e1 = c.demap(z, 1.7f, h1, c1), e2 = nv::FastPHY::demap(z, 1.7f, b, h2, c2);
            CHECK(std::abs(e1 - e2) < 1e-6, "%d bits: error %g vs %g", b, e1, e2);
            for (int k = 0; k < b; ++k) {
                CHECK(h1[k] == h2[k], "%d bits: decision %d differs", b, k);
                // QPSK: identical. 16-QAM: the original demapper (still used
                // for 16-QAM) gives distance to the nearest boundary, an
                // approximation; the general demapper (5..8 bits and BPSK)
                // gives the exact max-log metric, which can only be larger.
                if (b == 2)
                    CHECK(std::abs(c1[k] - c2[k]) < 1e-5 * (1 + c2[k]), "%d bits: confidence %d %g vs %g", b, k, c1[k], c2[k]);
                else
                    CHECK(c1[k] >= c2[k] - 1e-5f, "%d bits: max-log %g below the boundary distance %g", b, c1[k], c2[k]);
            }
            int bits[8];
            for (int k = 0; k < b; ++k) bits[k] = h2[k];
            const nv::C orig = b == 2 ? nv::C(bits[0] ? -nv::Q : nv::Q, bits[1] ? -nv::Q : nv::Q)
                                      : nv::C((1 - 2 * bits[0]) * (1 + 2 * bits[1]) * nv::A16, (1 - 2 * bits[2]) * (1 + 2 * bits[3]) * nv::A16);
            CHECK(c.map(bits) == orig, "%d bits: mapping differs from the original", b);
        }
    }
    // Frames of every order decode through noise (uncoded, CRC-32).
    {
        std::vector<int> data, pilots;
        for (int k = 1; k <= 22; ++k) (k % 11 == 6 ? pilots : data).push_back(k);
        for (int k = 42; k <= 63; ++k) (k % 11 == 2 ? pilots : data).push_back(k);
        nv::FramedPHY phy(64, 8, data, pilots, 1500, 0);
        std::normal_distribution<float> g(0, 1);
        for (int b = 1; b <= 8; ++b) {
            std::vector<unsigned char> payload(700), info(5, 0x5a);
            for (auto &x : payload) x = static_cast<unsigned char>(rng());
            const int ns = phy.frame_samples(int(payload.size()), b);
            std::vector<nv::C> iq(3000 + ns + 3000);
            const double snr_db = 12 + 4.0 * b;
            const float sigma = float(std::sqrt(std::pow(10.0, -snr_db / 10) / 2) * std::sqrt(double(data.size() + pilots.size()) / 64));
            phy.write(info.data(), 0, b, payload.data(), int(payload.size()), iq.data() + 3000, ns);
            for (auto &z : iq) z += nv::C(sigma * g(rng), sigma * g(rng));
            nv::FramedStream stream(phy);
            std::vector<nv::FramedStream::Result> results;
            stream.feed(iq.data(), int(iq.size()), true, results);
            bool ok = false;
            for (auto &r : results) ok |= r.payload_valid && r.payload == payload && r.header.bps == b;
            CHECK(ok, "%d bits per tone: frame not decoded at %.0f dB", b, snr_db);
            if (b > 1)
                CHECK(phy.frame_samples(700, b) < phy.frame_samples(700, b - 1), "%d bits: frame not shorter", b);
        }
    }
    std::printf("constellations 1..8 bits: power, Gray labels, round trip, boundaries, QPSK/16-QAM identity, frames\n");
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
