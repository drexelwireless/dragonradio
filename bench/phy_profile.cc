// P4 stage profile of native OFDM and liquid OFDM through the production PHY
// classes (NativeOFDM, liquid::OFDM), on one pinned core.
//
// For each configuration and link (strong and weak SNR) this modulates N
// packets with PHY::PacketModulator, places them in one stream separated by
// noise-only gaps (bursty traffic), adds complex AWGN, and demodulates the
// stream with PHY::PacketDemodulator in fixed chunks (real acquisition, no
// genie timing). It reports:
//   * thread CPU time and wall time for demodulation, per attempt (frame
//     sent) and per verified packet (delivered with correct bytes), plus the
//     idle cost per sample from a noise-only stream of the same length;
//   * modulation time per packet;
//   * a statistical stage breakdown from bench/sampler.hh (perf is not
//     available in the container): acquisition/detection, FFT and
//     equalization, demapping, FEC, CRC, Chase search, memory traffic,
//     packet handoff;
//   * cold construction (PHY object, first modulator/demodulator, FFTW
//     planning) in `cold` mode, run in a fresh process.
//
// Usage: phy_profile profile OUT.json [packets] [payload] [chunk]
//        phy_profile cold native|liquid
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <sys/resource.h>
#include <time.h>

#include "Packet.hh"
#include "phy/OFDM.hh"
#include "phy/NativeOFDM.hh"

#include "phy_bench.hh"
#include "sampler.hh"

using namespace bench;


namespace {

// Stage of one sample from its inline chain (innermost first).
std::string stage_of(const std::vector<std::string> &chain)
{
    if (chain.empty()) return "unknown";
    const std::string &leaf = chain[0];
    auto has = [](const std::string &s, const char *p) { return s.find(p) != std::string::npos; };
    if (has(leaf, "libc.so") && (has(leaf, "mem") || has(leaf, "malloc") || has(leaf, "free") || has(leaf, "cfree")))
        return "memory (memcpy/memmove/memset/malloc)";
    static const std::vector<std::pair<const char *, const char *>> rules = {
        // native engine
        {"chase_search", "Chase search"},
        {"crc32", "CRC"},
        {"crc_", "CRC"},
        {"soft_bits", "demapping"},
        {"FastPHY::demap", "demapping"},
        {"modemcf_demodulate", "demapping"},
        {"modem_demodulate", "demapping"},
        {"libfec.so", "FEC"},
        {"fec::", "FEC"},
        {"packetizer", "FEC"},
        {"fec_", "FEC"},
        {"interleaver", "FEC"},
        {"scramble", "FEC"},
        {"FastPHY::prepare", "FFT and equalization"},
        {"FastPHY::invert", "FFT and equalization"},
        {"FastPHY::symbol", "FFT and equalization"},
        {"phase_line", "FFT and equalization"},
        {"rxsymbol", "FFT and equalization"},
        {"estimate_eqgain", "FFT and equalization"},
        {"ofdmframesync_debug", "FFT and equalization"},
        {"FastPHY::refine", "acquisition/detection"},
        {"Autocorr", "acquisition/detection"},
        {"lag_products", "acquisition/detection"},
        {"energies", "acquisition/detection"},
        {"seekplcp", "acquisition/detection"},
        {"execute_S0a", "acquisition/detection"},
        {"execute_S0b", "acquisition/detection"},
        {"execute_S1", "acquisition/detection"},
        {"S0_metrics", "acquisition/detection"},
        {"estimate_gain_S0", "acquisition/detection"},
        {"estimate_gain_S1", "acquisition/detection"},
        {"estimate_cfo", "acquisition/detection"},
        {"windowcf", "acquisition/detection"},
        {"FramedStream::compact", "memory (memcpy/memmove/memset/malloc)"},
        {"FramedStream::rssi", "RSSI/EVM metrics"},
        {"mkRadioPacket", "packet handoff"},
        {"PacketDemodulator::deliver", "packet handoff"},
        {"callback", "packet handoff"},
        {"FramedPHY::decode_payload", "payload (other)"},
        {"FramedPHY::decode_header", "header (other)"},
        {"rxheader", "header (other)"},
        {"decode_header", "header (other)"},
        {"rxpayload", "payload (other)"},
        {"FramedStream::feed", "acquisition/detection"},
        {"ofdmframesync_execute", "acquisition/detection"},
        {"FramedPHY::write", "modulation"},
        {"FastPHY::write", "modulation"},
        {"ofdmflexframegen", "modulation"},
        {"ofdmframegen", "modulation"},
        {"PacketModulator::modulate", "modulation"},
    };
    for (const auto &fn : chain)
        for (const auto &[pat, st] : rules)
            if (has(fn, pat)) return st;
    if (has(leaf, "libfftw3f")) return "FFT (unattributed)";
    return "other";
}

struct Profile {
    size_t samples = 0;
    std::map<std::string, size_t> stages, leaves;
};

Profile summarize(const bench::Sampler &s)
{
    Profile p;
    auto chains = s.symbolize();
    p.samples = chains.size();
    for (const auto &c : chains) {
        p.stages[stage_of(c)]++;
        p.leaves[c.empty() ? "?" : c[0]]++;
    }
    return p;
}

void print_profile(FILE *f, const Profile &p, const char *indent)
{
    std::fprintf(f, "%s\"samples\": %zu,\n%s\"stages\": {", indent, p.samples, indent);
    bool first = true;
    for (auto &[k, v] : p.stages) {
        std::fprintf(f, "%s\"%s\": %.4f", first ? "" : ", ", k.c_str(), p.samples ? double(v) / p.samples : 0.0);
        first = false;
    }
    std::fprintf(f, "},\n%s\"top_leaves\": [", indent);
    std::vector<std::pair<size_t, std::string>> top;
    for (auto &[k, v] : p.leaves) top.push_back({v, k});
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < std::min<size_t>(12, top.size()); ++i) {
        std::string name = top[i].second;
        for (auto &ch : name) if (ch == '"' || ch == '\\') ch = '\'';
        std::fprintf(f, "%s[\"%s\", %.4f]", i ? ", " : "", name.c_str(), p.samples ? double(top[i].first) / p.samples : 0.0);
    }
    std::fprintf(f, "]");
}

struct Packets {
    std::vector<std::vector<unsigned char>> bodies;
    std::vector<fc32> stream;
    std::vector<std::pair<size_t, size_t>> frames;
    double mod_cpu = 0, mod_wall = 0;
    Profile mod_profile;
};

Packets modulate(PHY &phy, unsigned npkts, size_t payload, std::mt19937_64 &rng, bool profile)
{
    Packets out;
    auto mod = phy.mkPacketModulator();
    std::uniform_int_distribution<int> gapdist(200, 600);
    out.stream.resize(1000);
    bench::Sampler sampler;
    std::vector<std::shared_ptr<NetPacket>> pkts;
    for (unsigned i = 0; i < npkts; ++i) {
        auto pkt = mkPacket(i, payload, rng);
        out.bodies.emplace_back(pkt->data(), pkt->data() + pkt->size());
        pkts.push_back(pkt);
    }
    std::vector<ModPacket> mpkts(npkts);
    if (profile) sampler.start(250);
    const double c0 = thread_cpu(), w0 = wall();
    for (unsigned i = 0; i < npkts; ++i)
        mod->modulate(pkts[i], 1.0f, mpkts[i]);
    out.mod_cpu = thread_cpu() - c0;
    out.mod_wall = wall() - w0;
    if (profile) {
        // More passes (discarded) so the sampler sees about 2 s of modulation.
        std::vector<ModPacket> spare(npkts);
        for (double used = out.mod_cpu; used < 2.0;) {
            const double c1 = thread_cpu();
            for (unsigned i = 0; i < npkts; ++i) {
                auto copy = std::make_shared<NetPacket>(*pkts[i]);
                mod->modulate(copy, 1.0f, spare[i]);
            }
            used += thread_cpu() - c1;
        }
        sampler.stop();
        out.mod_profile = summarize(sampler);
    }
    for (auto &m : mpkts) {
        const fc32 *x = m.samples->data() + m.offset;
        out.frames.push_back({out.stream.size(), m.nsamples});
        out.stream.insert(out.stream.end(), x, x + m.nsamples);
        out.stream.resize(out.stream.size() + gapdist(rng));
    }
    out.stream.resize(out.stream.size() + 3000);
    return out;
}

struct DemodResult {
    unsigned delivered = 0, payload_fail = 0, corrupt = 0;
    double cpu = 0, wall = 0, cpu_min = 0, cpu_max = 0;
    size_t reps = 0;
    Profile profile;
};

DemodResult demodulate(PHY &phy, const std::vector<fc32> &stream, const Packets *pk, size_t chunk, bool profile)
{
    // Repeat with a fresh demodulator until at least kMinCPU seconds of CPU
    // (and kMinReps repetitions) so the 1 kHz sampler collects enough
    // samples; report the median repetition. Outcomes are from the first.
    constexpr double kMinCPU = 3.0;
    constexpr int kMinReps = 3, kMaxReps = 200;
    DemodResult r;
    bench::Sampler sampler;
    std::vector<double> cpus, walls;
    std::vector<std::shared_ptr<RadioPacket>> got;
    got.reserve(pk ? pk->bodies.size() * 2 : 16);
    if (profile) sampler.start(500);
    double total = 0;
    for (int rep = 0; rep < kMaxReps && (rep < kMinReps || total < kMinCPU); ++rep) {
        auto demod = phy.mkPacketDemodulator();
        demod->setCallback([&, rep](std::shared_ptr<RadioPacket> &&p) { if (p && rep == 0) got.push_back(std::move(p)); });
        demod->reset(Channel(0.0, 1e6));
        demod->timestamp(MonoClock::time_point{100.0}, std::nullopt, 0, 0, 1.0f, 1e6f);
        const double c0 = thread_cpu(), w0 = wall();
        for (size_t i = 0; i < stream.size(); i += chunk)
            demod->demodulate(stream.data() + i, std::min(chunk, stream.size() - i));
        cpus.push_back(thread_cpu() - c0);
        walls.push_back(wall() - w0);
        total += cpus.back();
    }
    if (profile) { sampler.stop(); r.profile = summarize(sampler); }
    r.reps = cpus.size();
    std::sort(cpus.begin(), cpus.end());
    std::sort(walls.begin(), walls.end());
    r.cpu = cpus[cpus.size() / 2];
    r.wall = walls[walls.size() / 2];
    r.cpu_min = cpus.front();
    r.cpu_max = cpus.back();
    if (!pk) return r;
    const unsigned npkts = pk->bodies.size();
    std::vector<bool> seen(npkts, false);
    for (auto &p : got) {
        const unsigned seq = static_cast<uint16_t>(p->hdr.seq);
        if (seq >= npkts || p->hdr.curhop != 1) continue;
        if (p->internal_flags.invalid_payload) {
            if (!seen[seq]) r.payload_fail++;
            seen[seq] = true;
            continue;
        }
        const auto &b = pk->bodies[seq];
        if (p->size() != b.size() || std::memcmp(p->data(), b.data(), b.size()) != 0) { r.corrupt++; continue; }
        if (!seen[seq]) r.delivered++;
        seen[seq] = true;
    }
    return r;
}

std::vector<fc32> add_noise(const Packets &pk, double snr_db, std::mt19937_64 &rng, double &n0)
{
    n0 = frame_power(pk.stream, pk.frames) / std::pow(10.0, snr_db / 10.0);
    std::vector<fc32> out(pk.stream);
    add_awgn(out, n0, rng);
    return out;
}

int cold(const std::string &which)
{
    // Fresh process: the first construction pays FFTW's one-time setup.
    Config c = which == "native" ? configs()[2] : configs()[6];
    double t0 = wall(), c0 = thread_cpu();
    auto phy = mkPHY(c);
    double t1 = wall(), c1 = thread_cpu();
    auto mod = phy->mkPacketModulator();
    double t2 = wall(), c2 = thread_cpu();
    auto demod = phy->mkPacketDemodulator();
    double t3 = wall(), c3 = thread_cpu();
    // Warm: the same again in this process (planner already initialized).
    auto phy2 = mkPHY(c);
    double t4 = wall();
    auto mod2 = phy2->mkPacketModulator();
    double t5 = wall();
    auto demod2 = phy2->mkPacketDemodulator();
    double t6 = wall();
    size_t gm = phy->getModulatedSize(0, 1000);
    double t7 = wall();
    std::printf("{\"phy\": \"%s\", \"construct_us\": %.1f, \"first_modulator_us\": %.1f, \"first_demodulator_us\": %.1f, "
                "\"construct_cpu_us\": %.1f, \"modulator_cpu_us\": %.1f, \"demodulator_cpu_us\": %.1f, "
                "\"warm_construct_us\": %.1f, \"warm_modulator_us\": %.1f, \"warm_demodulator_us\": %.1f, "
                "\"getModulatedSize_us\": %.1f, \"frame_samples_1000B\": %zu}\n",
                which.c_str(), (t1 - t0) * 1e6, (t2 - t1) * 1e6, (t3 - t2) * 1e6,
                (c1 - c0) * 1e6, (c2 - c1) * 1e6, (c3 - c2) * 1e6,
                (t4 - t3) * 1e6, (t5 - t4) * 1e6, (t6 - t5) * 1e6, (t7 - t6) * 1e6, gm);
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc >= 3 && std::string(argv[1]) == "cold") {
        PHY::setTeam(0);
        PHY::setNodeId(2);
        return cold(argv[2]);
    }
    if (argc < 3 || std::string(argv[1]) != "profile") {
        std::fprintf(stderr, "usage: %s profile OUT.json [packets] [payload] [chunk] | cold native|liquid\n", argv[0]);
        return 2;
    }
    const unsigned npkts = argc > 3 ? std::atoi(argv[3]) : 1000;
    const size_t payload = argc > 4 ? std::atoi(argv[4]) : 1000;
    const size_t chunk = argc > 5 ? std::atoi(argv[5]) : 2048;
    PHY::setTeam(0);
    PHY::setNodeId(2);

    FILE *f = std::fopen(argv[2], "w");
    std::fprintf(f, "{\"packets\": %u, \"payload\": %zu, \"chunk\": %zu, \"results\": [\n", npkts, payload, chunk);
    bool first = true;
    for (const Config &c : configs()) {
        auto phy = mkPHY(c);
        std::mt19937_64 rng(42);
        Packets pk = modulate(*phy, npkts, payload, rng, true);
        size_t frame_samples = 0;
        for (auto [o, n] : pk.frames) frame_samples += n;
        // Idle cost: noise only, same length and level as the strong stream.
        double n0 = 0;
        std::vector<fc32> strong = add_noise(pk, 25.0, rng, n0);
        std::normal_distribution<float> noise(0.0f, static_cast<float>(std::sqrt(n0 / 2)));
        std::vector<fc32> idle(pk.stream.size());
        for (auto &z : idle) z = fc32(noise(rng), noise(rng));
        DemodResult id = demodulate(*phy, idle, nullptr, chunk, true);
        const double idle_ns_per_sample = id.cpu / idle.size() * 1e9;
        std::fprintf(f, "%s{\"config\": \"%s\", \"phy\": \"%s\", \"chase\": %u, \"frame_samples\": %.1f, \"stream_samples\": %zu,\n"
                     " \"modulation\": {\"cpu_us_per_packet\": %.2f, \"wall_us_per_packet\": %.2f,\n  ",
                     first ? "" : ",\n", c.name.c_str(), c.phy.c_str(), c.chase, double(frame_samples) / npkts, pk.stream.size(),
                     pk.mod_cpu / npkts * 1e6, pk.mod_wall / npkts * 1e6);
        first = false;
        print_profile(f, pk.mod_profile, "  ");
        std::fprintf(f, "},\n \"idle\": {\"cpu_ns_per_sample\": %.3f, \"msps_one_core\": %.3f,\n  ", idle_ns_per_sample, 1e3 / idle_ns_per_sample);
        print_profile(f, id.profile, "  ");
        std::fprintf(f, "},\n \"links\": [");
        int li = 0;
        for (double snr : {25.0, c.weak_db}) {
            std::vector<fc32> rx = li == 0 ? strong : add_noise(pk, snr, rng, n0);
            DemodResult r = demodulate(*phy, rx, &pk, chunk, true);
            const double extra = r.cpu - idle_ns_per_sample * 1e-9 * rx.size();
            std::fprintf(f, "%s\n  {\"link\": \"%s\", \"snr_db\": %.1f, \"sent\": %u, \"delivered\": %u, \"payload_fail\": %u, \"corrupt\": %u,"
                         " \"per\": %.4f, \"reps\": %zu, \"cpu_s\": %.4f, \"cpu_s_min\": %.4f, \"cpu_s_max\": %.4f, \"wall_s\": %.4f, \"cpu_us_per_attempt\": %.2f, \"cpu_us_per_verified\": %.2f,"
                         " \"frame_cpu_us_per_attempt\": %.2f, \"cpu_ns_per_sample\": %.3f, \"msps_one_core\": %.3f,\n   ",
                         li ? "," : "", li ? "weak" : "strong", snr, npkts, r.delivered, r.payload_fail, r.corrupt,
                         1.0 - double(r.delivered) / npkts, r.reps, r.cpu, r.cpu_min, r.cpu_max, r.wall, r.cpu / npkts * 1e6,
                         r.delivered ? r.cpu / r.delivered * 1e6 : 0.0, extra / npkts * 1e6,
                         r.cpu / rx.size() * 1e9, rx.size() / r.cpu / 1e6);
            print_profile(f, r.profile, "   ");
            std::fprintf(f, "}");
            std::fprintf(stderr, "%-28s %-6s %5.1f dB delivered %4u/%u  %8.1f us/attempt %8.1f us/verified  idle %.1f ns/sample\n",
                         c.name.c_str(), li ? "weak" : "strong", snr, r.delivered, npkts, r.cpu / npkts * 1e6,
                         r.delivered ? r.cpu / r.delivered * 1e6 : 0.0, idle_ns_per_sample);
            ++li;
        }
        std::fprintf(f, "]}");
        std::fflush(f);
    }
    std::fprintf(f, "\n]}\n");
    std::fclose(f);
    return 0;
}
