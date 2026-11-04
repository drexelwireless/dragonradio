// Framed native downlink OFDMA engine (P5; src/simulation/NativeOFDMA.hh).
//
//  * Two users in one burst, equal and unequal allocations, different
//    constellations and codes: each receiver decodes its own slot exactly,
//    and the frame is one shared IQ buffer of the longer user's length.
//  * A receiver that wants only its own slot never demodulates the partner's.
//  * One user idle: single-user allocation (every tone to slot 0).
//  * Strong/weak: the same burst received at high SNR by one UE and low SNR
//    by another; the weak UE's failures do not affect the strong UE.
//  * Corrupted control header: no result, and a following frame is found.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "simulation/NativeOFDMA.hh"

namespace nv = dragonradio::native;
namespace nf = dragonradio::native::fec;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; if (failures < 40) { std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } } while (0)

struct Frame {
    int alloc;
    std::array<nv::OFDMASlotInput, nv::kMaxUsers> in;   // pointers rebound by bind()
    std::array<std::vector<unsigned char>, nv::kMaxUsers> payload, info;
    // Point the slot inputs at this object's own buffers (after any copy).
    const std::array<nv::OFDMASlotInput, nv::kMaxUsers> &bind() {
        for (int u = 0; u < nv::kMaxUsers; ++u)
            if (!info[u].empty()) { in[u].info = info[u].data(); in[u].payload = payload[u].data(); }
            else in[u].info = nullptr;
        return in;
    }
};

static std::mt19937 rng(23);

static Frame mkFrame(int alloc, int n0, int bps0, nf::Code c0, int n1, int bps1, nf::Code c1, unsigned char seq)
{
    Frame f;
    f.alloc = alloc;
    const int len[2] = {n0, n1}, bps[2] = {bps0, bps1};
    const nf::Code code[2] = {c0, c1};
    for (int u = 0; u < 2; ++u) {
        if (len[u] < 0) continue;   // empty slot
        f.payload[u].resize(len[u]);
        for (auto &b : f.payload[u]) b = static_cast<unsigned char>(rng());
        f.info[u] = {1, static_cast<unsigned char>(2 + u), seq, 0, 0};   // curhop 1, nexthop 2 or 3
        f.in[u].info = f.info[u].data();
        f.in[u].mcsidx = u;
        f.in[u].bps = bps[u];
        f.in[u].payload = f.payload[u].data();
        f.in[u].length = len[u];
        f.in[u].fec0 = code[u];
    }
    return f;
}

static std::vector<nv::C> transmit(nv::FramedOFDMA &phy, std::vector<Frame> &frames, std::vector<long long> &starts)
{
    std::vector<nv::C> iq(2000);
    for (auto &f : frames) {
        const int ns = phy.frame_samples(f.alloc, f.bind());
        starts.push_back(iq.size());
        iq.resize(iq.size() + ns);
        const int wrote = phy.write(f.alloc, f.bind(), iq.data() + starts.back(), ns);
        CHECK(wrote == ns, "frame_samples %d != write %d", ns, wrote);
        iq.resize(iq.size() + 300);
    }
    iq.resize(iq.size() + 2000);
    return iq;
}

static void noise(std::vector<nv::C> &iq, double snr_db)
{
    std::normal_distribution<float> g(0, 1);
    const float sigma = float(std::sqrt(std::pow(10.0, -snr_db / 10) / 2));
    for (auto &z : iq) z += nv::C(sigma * g(rng), sigma * g(rng));
}

// Receive as UE `me` (nexthop 2 for slot 0's user, 3 for slot 1's).
static std::vector<nv::FramedOFDMAStream::Result> receive(nv::FramedOFDMA &phy, const std::vector<nv::C> &iq, int me, size_t chunk)
{
    nv::FramedOFDMAStream stream(phy, [me](const nv::OFDMAUser &u) { return u.info[1] == me; });
    std::vector<nv::FramedOFDMAStream::Result> out;
    for (size_t i = 0; i < iq.size(); i += chunk)
        stream.feed(iq.data() + i, int(std::min(chunk, iq.size() - i)), i + chunk >= iq.size(), out);
    return out;
}

int main()
{
    std::vector<int> data, pilots;
    for (int k = 1; k <= 22; ++k) (k % 11 == 6 ? pilots : data).push_back(k);
    for (int k = 42; k <= 63; ++k) (k % 11 == 2 ? pilots : data).push_back(k);
    nv::FramedOFDMA phy(64, 8, data, pilots, 1600, nf::encoded_length(1600, nf::kRS8, nf::kNone));

    std::vector<Frame> frames = {
        mkFrame(0, 500, 2, nf::kNone, 500, 2, nf::kNone, 1),      // equal, both QPSK uncoded
        mkFrame(0, 900, 4, nf::kRS8, 200, 2, nf::kRS8, 2),       // different lengths and orders
        mkFrame(1, 300, 2, nf::kV27, 1000, 6, nf::kRS8, 3),      // unequal 1/3 : 2/3
        mkFrame(2, 1200, 8, nf::kNone, 64, 1, nf::kNone, 4),     // unequal 2/3 : 1/3, 256-QAM and BPSK
        mkFrame(3, 1500, 4, nf::kRS8, -1, 2, nf::kNone, 5),      // one user idle: every tone to slot 0
        mkFrame(0, 0, 2, nf::kNone, 400, 2, nf::kNone, 6),       // empty payload in slot 0
    };
    std::vector<long long> starts;
    auto iq = transmit(phy, frames, starts);
    // Shared IQ: the burst is as long as the longer user needs.
    for (auto &f : frames) {
        f.bind();
        int P = 0;
        for (int u = 0; u < 2; ++u)
            if (f.in[u].info) P = std::max(P, phy.user_symbols(f.alloc, u, f.in[u].length, f.in[u].bps, f.in[u].fec0, f.in[u].fec1));
        CHECK(phy.frame_samples(f.alloc, f.in) == phy.frame_samples(P), "frame length is the longer user's");
    }

    auto check_ue = [&](const char *what, const std::vector<nv::C> &rx, int me, size_t chunk, bool expect_ok) {
        auto res = receive(phy, rx, me, chunk);
        const int slot = me - 2;
        unsigned ok = 0, decoded_partner = 0, expected = 0;
        for (size_t k = 0; k < frames.size(); ++k) {
            const auto &f = frames[k];
            if (!f.in[slot].info) continue;
            ++expected;
            for (const auto &r : res) {
                if (r.slot != slot || r.start != starts[k]) continue;
                if (r.payload_valid && r.payload == f.payload[slot]) ++ok;
            }
        }
        for (const auto &r : res)
            if (r.slot >= 0 && r.slot != slot && (r.wanted || !r.payload.empty())) ++decoded_partner;
        CHECK(decoded_partner == 0, "%s: UE %d demodulated %u partner slots", what, me, decoded_partner);
        if (expect_ok)
            CHECK(ok == expected, "%s: UE %d decoded %u of %u", what, me, ok, expected);
        std::printf("%-40s UE %d: %u of %u own payloads exact\n", what, me, ok, expected);
        return ok;
    };
    for (size_t chunk : {size_t(97), size_t(4096), iq.size()}) {
        check_ue("clean", iq, 2, chunk, true);
        check_ue("clean", iq, 3, chunk, true);
    }
    // Strong / weak: one burst stream, two receivers at different SNR.
    {
        auto strong = iq, weak = iq;
        noise(strong, 35.0);
        noise(weak, 3.0);
        check_ue("strong UE (35 dB)", strong, 2, 4096, true);
        const unsigned w = check_ue("weak UE (3 dB): failures expected", weak, 3, 4096, false);
        CHECK(w < 5, "weak UE at 3 dB decoded %u (expected mostly failures)", w);
    }
    // A broken control header yields nothing and the next frame is still found.
    {
        auto rx = iq;
        const int L = phy.length_samples();
        for (int k = 0; k < 3 * L; ++k)   // wipe frame 0's control section
            rx[starts[0] + 2 * L + k] = nv::C(0.3f, -0.3f);
        auto res = receive(phy, rx, 2, 4096);
        bool first = false, second = false;
        for (const auto &r : res) {
            first |= r.start == starts[0] && r.payload_valid;
            second |= r.start == starts[1] && r.slot == 0 && r.payload_valid && r.payload == frames[1].payload[0];
        }
        CHECK(!first, "frame with a corrupted control header delivered");
        CHECK(second, "frame after a corrupted control header not found");
    }
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
