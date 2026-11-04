// Packet error rate of coded and uncoded MCS over AWGN for the native OFDM
// PHY and liquid OFDM, through the production PHY classes (P3).
//
// Each point modulates N packets with PHY::PacketModulator, places them in one
// stream separated by noise-only gaps, adds complex white Gaussian noise at
// the requested per-sample SNR (signal power measured over the frames), and
// demodulates the whole stream with PHY::PacketDemodulator in 4096-sample
// chunks: real acquisition, channel estimation and decoding, no genie timing.
// Outcomes per packet: delivered (payload valid, bytes identical), payload
// failure (header valid, payload invalid), header/acquisition failure (no
// packet). Demodulation time per packet is measured on one thread per job.
//
// Usage: fec_per OUT.csv [packets-per-point] [payload-bytes]
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Packet.hh"
#include "phy/OFDM.hh"
#include "phy/NativeOFDM.hh"

using fc32 = std::complex<float>;

struct Config {
    std::string name;
    std::string phy;        // "native" or "liquid"
    liquid::MCS mcs;
    bool soft;
    unsigned chase;
    std::string header = "none";   // native header section code
};

struct Point {
    std::string config;
    double snr_db;
    unsigned sent = 0, delivered = 0, payload_fail = 0, header_fail = 0, corrupt = 0;
    double measured_snr_db = 0, demod_us = 0, mod_us = 0, frame_samples = 0;
};

static std::shared_ptr<PHY> mkPHY(const Config &c)
{
    std::vector<std::pair<liquid::MCS, AutoGain>> table{{c.mcs, AutoGain()}};
    if (c.phy == "native")
        return std::make_shared<NativeOFDM>(table, 48, 6, std::nullopt, 4096, c.chase, c.soft, c.header);
    // Production liquid header: crc32, v29p78, BPSK; taper 4 as the radio default.
    const liquid::MCS header(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_BPSK);
    return std::make_shared<liquid::OFDM>(header, table, c.soft, c.soft, 48, 6, 4, std::nullopt);
}

static Point run(const Config &c, double snr_db, unsigned npkts, size_t payload, uint64_t seed)
{
    Point pt;
    pt.config = c.name;
    pt.snr_db = snr_db;
    // The production PHY classes serialize FFTW planning (fftw::mutex) themselves.
    std::shared_ptr<PHY> phy = mkPHY(c);
    auto mod = phy->mkPacketModulator();
    auto demod = phy->mkPacketDemodulator();
    std::mt19937_64 rng(seed);

    // Packets: node 1 -> node 2, random data.
    std::vector<std::vector<unsigned char>> bodies;
    std::vector<fc32> stream;
    std::vector<std::pair<size_t, size_t>> frames;
    std::uniform_int_distribution<int> gapdist(200, 600);
    stream.resize(1000);
    double mod_ns = 0;
    for (unsigned i = 0; i < npkts; ++i) {
        auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + payload);
        pkt->hdr.curhop = 1;
        pkt->hdr.nexthop = 2;
        pkt->hdr.seq = Seq{static_cast<uint16_t>(i)};
        pkt->hdr.flags = {0};
        pkt->hdr.flags.has_seq = 1;
        pkt->ehdr().src = 1;
        pkt->ehdr().dest = 2;
        pkt->ehdr().data_len = payload;
        unsigned char *d = pkt->data() + sizeof(ExtendedHeader);
        for (size_t k = 0; k < payload; ++k) d[k] = static_cast<unsigned char>(rng());
        bodies.emplace_back(pkt->data(), pkt->data() + pkt->size());
        pkt->mcsidx = 0;
        ModPacket mpkt;
        auto t0 = std::chrono::steady_clock::now();
        mod->modulate(pkt, 1.0f, mpkt);
        mod_ns += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        const fc32 *x = mpkt.samples->data() + mpkt.offset;
        frames.push_back({stream.size(), mpkt.nsamples});
        stream.insert(stream.end(), x, x + mpkt.nsamples);
        stream.resize(stream.size() + gapdist(rng));
    }
    stream.resize(stream.size() + 3000);
    pt.mod_us = mod_ns / npkts / 1e3;

    // Signal power over frame samples; noise at the requested per-sample SNR.
    double power = 0, nsig = 0;
    for (auto [off, n] : frames) {
        for (size_t k = 0; k < n; ++k) power += std::norm(stream[off + k]);
        nsig += n;
    }
    power /= nsig;
    pt.frame_samples = nsig / npkts;
    const double n0 = power / std::pow(10.0, snr_db / 10.0);
    std::normal_distribution<float> noise(0.0f, static_cast<float>(std::sqrt(n0 / 2)));
    double npow = 0;
    for (auto &z : stream) {
        fc32 w(noise(rng), noise(rng));
        npow += std::norm(w);
        z += w;
    }
    pt.measured_snr_db = 10 * std::log10(power / (npow / stream.size()));

    // Demodulate.
    std::vector<std::shared_ptr<RadioPacket>> got;
    demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) { if (p) got.push_back(std::move(p)); });
    demod->reset(Channel(0.0, 1e6));
    demod->timestamp(MonoClock::time_point{100.0}, std::nullopt, 0, 0, 1.0f, 1e6f);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < stream.size(); i += 4096)
        demod->demodulate(stream.data() + i, std::min<size_t>(4096, stream.size() - i));
    pt.demod_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / npkts;

    pt.sent = npkts;
    std::vector<bool> seen(npkts, false);
    for (auto &p : got) {
        const unsigned seq = static_cast<uint16_t>(p->hdr.seq);
        if (seq >= npkts || p->hdr.curhop != 1)
            continue;
        if (p->internal_flags.invalid_payload) {
            if (!seen[seq]) pt.payload_fail++;
            seen[seq] = true;
            continue;
        }
        const auto &b = bodies[seq];
        if (p->size() != b.size() || std::memcmp(p->data(), b.data(), b.size()) != 0) {
            pt.corrupt++;   // passed CRC with wrong bytes (should be ~never)
            continue;
        }
        if (!seen[seq]) pt.delivered++;
        seen[seq] = true;
    }
    pt.header_fail = pt.sent - pt.delivered - pt.payload_fail;
    return pt;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s OUT.csv [packets] [payload]\n", argv[0]);
        return 2;
    }
    const unsigned npkts = argc > 2 ? std::atoi(argv[2]) : 400;
    const size_t payload = argc > 3 ? std::atoi(argv[3]) : 1000;
    PHY::setTeam(0);
    PHY::setNodeId(2);

    auto M = [](fec_scheme f0, fec_scheme f1, modulation_scheme ms) { return liquid::MCS(LIQUID_CRC_32, f0, f1, ms); };
    const auto N = LIQUID_FEC_NONE;
    std::vector<Config> configs = {
        {"native qpsk uncoded chase0", "native", M(N, N, LIQUID_MODEM_QPSK), true, 0},
        {"native qpsk uncoded chase4", "native", M(N, N, LIQUID_MODEM_QPSK), true, 4},
        {"native qpsk rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QPSK), true, 0},
        {"native qpsk v27 soft", "native", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), true, 0},
        {"native qpsk v27 hard", "native", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), false, 0},
        {"native qpsk v29p78 soft", "native", M(N, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_QPSK), true, 0},
        {"native qam16 uncoded chase0", "native", M(N, N, LIQUID_MODEM_QAM16), true, 0},
        {"native qam16 rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QAM16), true, 0},
        // The same with the header section coded (v29 r1/2, soft), as the
        // production default (--native-header-fec v29).
        {"native+v29hdr qpsk uncoded chase0", "native", M(N, N, LIQUID_MODEM_QPSK), true, 0, "v29"},
        {"native+v29hdr qpsk rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QPSK), true, 0, "v29"},
        {"native+v29hdr qpsk v27 soft", "native", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), true, 0, "v29"},
        {"native+v29hdr qpsk v27 hard", "native", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), false, 0, "v29"},
        {"native+v29hdr qpsk v29p78 soft", "native", M(N, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_QPSK), true, 0, "v29"},
        {"native+v29hdr qam16 rs8", "native", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QAM16), true, 0, "v29"},
        {"liquid qpsk uncoded", "liquid", M(N, N, LIQUID_MODEM_QPSK), true, 0},
        {"liquid qpsk rs8", "liquid", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QPSK), true, 0},
        {"liquid qpsk v27 soft", "liquid", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), true, 0},
        {"liquid qpsk v27 hard", "liquid", M(N, LIQUID_FEC_CONV_V27, LIQUID_MODEM_QPSK), false, 0},
        {"liquid qpsk v29p78 soft", "liquid", M(N, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_QPSK), true, 0},
        {"liquid qam16 uncoded", "liquid", M(N, N, LIQUID_MODEM_QAM16), true, 0},
        {"liquid qam16 rs8", "liquid", M(LIQUID_FEC_RS_M8, N, LIQUID_MODEM_QAM16), true, 0},
    };
    struct Job { size_t c; double snr; };
    std::vector<Job> jobs;
    for (size_t c = 0; c < configs.size(); ++c) {
        const bool q16 = configs[c].mcs.ms == LIQUID_MODEM_QAM16;
        for (double snr = q16 ? 6.0 : -2.0; snr <= (q16 ? 22.0 : 14.0) + 1e-9; snr += 1.0)
            jobs.push_back({c, snr});
    }
    std::vector<Point> results(jobs.size());
    std::atomic<size_t> next{0};
    const unsigned nthreads = std::max(1u, std::thread::hardware_concurrency() / 2);
    std::vector<std::thread> pool;
    std::mutex print;
    for (unsigned t = 0; t < nthreads; ++t)
        pool.emplace_back([&] {
            for (size_t j; (j = next++) < jobs.size(); ) {
                results[j] = run(configs[jobs[j].c], jobs[j].snr, npkts, payload, 1000 + j);
                std::lock_guard<std::mutex> lock(print);
                const Point &p = results[j];
                std::fprintf(stderr, "%-30s %5.1f dB  delivered %4u/%u  hdr %4u  payload %4u  demod %7.1f us\n",
                             p.config.c_str(), p.snr_db, p.delivered, p.sent, p.header_fail, p.payload_fail, p.demod_us);
            }
        });
    for (auto &t : pool) t.join();

    FILE *f = std::fopen(argv[1], "w");
    std::fprintf(f, "config,snr_db,measured_snr_db,sent,delivered,payload_fail,header_fail,corrupt,per,demod_us,mod_us,frame_samples,payload_bytes\n");
    for (const Point &p : results)
        std::fprintf(f, "%s,%.1f,%.3f,%u,%u,%u,%u,%u,%.6f,%.2f,%.2f,%.1f,%zu\n", p.config.c_str(), p.snr_db, p.measured_snr_db,
                     p.sent, p.delivered, p.payload_fail, p.header_fail, p.corrupt,
                     1.0 - double(p.delivered) / p.sent, p.demod_us, p.mod_us, p.frame_samples, payload);
    std::fclose(f);
    unsigned corrupt = 0;
    for (const Point &p : results) corrupt += p.corrupt;
    std::fprintf(stderr, "wrote %s (%zu points); CRC-passing corrupt packets: %u\n", argv[1], results.size(), corrupt);
    return corrupt ? 1 : 0;
}
