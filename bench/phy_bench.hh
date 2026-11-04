// Shared helpers for the P4 PHY benchmarks (phy_profile.cc, stream_throughput.cc):
// production PHY construction, packet streams with noise gaps, timers.
#ifndef BENCH_PHY_BENCH_HH_
#define BENCH_PHY_BENCH_HH_

#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <time.h>

#include "Packet.hh"
#include "phy/OFDM.hh"
#include "phy/NativeOFDM.hh"

namespace bench {

using fc32 = std::complex<float>;

inline double thread_cpu()
{
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

inline double wall()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Config {
    std::string name;
    std::string phy;        // "native" or "liquid"
    liquid::MCS mcs;
    unsigned chase;
    double weak_db;         // near the waterfall (PER roughly 10-30%)
};

inline std::shared_ptr<PHY> mkPHY(const Config &c)
{
    std::vector<std::pair<liquid::MCS, AutoGain>> table{{c.mcs, AutoGain()}};
    if (c.phy == "native")   // production defaults: M=48, CP=6, v29 header, soft payload
        return std::make_shared<NativeOFDM>(table, 48, 6, std::nullopt, 4096, c.chase, true, "v29");
    // Production liquid OFDM: crc32 + v29p78 BPSK header, soft decoding, taper 4.
    const liquid::MCS header(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_BPSK);
    return std::make_shared<liquid::OFDM>(header, table, true, true, 48, 6, 4, std::nullopt);
}

inline std::vector<Config> configs()
{
    auto M = [](fec_scheme f0, fec_scheme f1, modulation_scheme ms) { return liquid::MCS(LIQUID_CRC_32, f0, f1, ms); };
    const auto N = LIQUID_FEC_NONE;
    // Weak SNRs sit about 1 dB below each code's 1% PER crossing from the P3
    // benchmark (docs/native-fec.rst); the measured PER is reported.
    return {
        {"native qpsk uncoded chase0", "native", M(N, N, LIQUID_MODEM_QPSK), 0, 11.0},
        {"native qpsk uncoded chase4", "native", M(N, N, LIQUID_MODEM_QPSK), 4, 11.0},
        {"native qpsk rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QPSK), 0, 6.8},
        {"native qpsk v27 soft", "native", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), 0, 3.3},
        {"native qam16 rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QAM16), 0, 13.6},
        {"liquid qpsk uncoded", "liquid", M(N, N, LIQUID_MODEM_QPSK), 0, 12.3},
        {"liquid qpsk rs8", "liquid", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QPSK), 0, 8.2},
        {"liquid qpsk v27 soft", "liquid", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), 0, 4.5},
        {"liquid qam16 rs8", "liquid", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QAM16), 0, 14.6},
    };
}

inline const Config &config(const std::string &name)
{
    static const std::vector<Config> all = configs();
    for (const auto &c : all)
        if (c.name == name) return c;
    throw std::invalid_argument("unknown configuration " + name);
}

inline std::shared_ptr<NetPacket> mkPacket(unsigned seq, size_t payload, std::mt19937_64 &rng)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + payload);
    pkt->hdr.curhop = 1;
    pkt->hdr.nexthop = 2;
    pkt->hdr.seq = Seq{static_cast<uint16_t>(seq)};
    pkt->hdr.flags = {0};
    pkt->hdr.flags.has_seq = 1;
    pkt->ehdr().src = 1;
    pkt->ehdr().dest = 2;
    pkt->ehdr().data_len = payload;
    unsigned char *d = pkt->data() + sizeof(ExtendedHeader);
    for (size_t k = 0; k < payload; ++k) d[k] = static_cast<unsigned char>(rng());
    pkt->mcsidx = 0;
    return pkt;
}

// Mean power over the frame samples of a stream.
inline double frame_power(const std::vector<fc32> &stream, const std::vector<std::pair<size_t, size_t>> &frames)
{
    double power = 0, nsig = 0;
    for (auto [off, n] : frames) {
        for (size_t k = 0; k < n; ++k) power += std::norm(stream[off + k]);
        nsig += n;
    }
    return nsig ? power / nsig : 0.0;
}

inline void add_awgn(std::vector<fc32> &x, double n0, std::mt19937_64 &rng)
{
    std::normal_distribution<float> noise(0.0f, static_cast<float>(std::sqrt(n0 / 2)));
    for (auto &z : x) z += fc32(noise(rng), noise(rng));
}

}  // namespace bench

#endif  // BENCH_PHY_BENCH_HH_
