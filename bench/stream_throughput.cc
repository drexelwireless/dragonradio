// P4 streaming receive throughput of the production PHY demodulators
// (NativeOFDM, liquid::OFDM) on one core per demodulator.
//
// A capture of about 2 s at 1 MS/s is built per traffic pattern:
//   idle       noise only
//   bursty     TDMA-like: frames back to back for 25 ms, then 25 ms of noise
//   saturated  frames back to back with 100-sample gaps
//   bursty-weak  bursty at a weak SNR (failed attempts, Chase where enabled)
// Frames are 1000-byte packets at 25 dB per-sample SNR (weak: the
// configuration's weak SNR). The capture is fed to PacketDemodulator in fixed
// chunks (363 samples, as an N210 1 GbE sc16 frame; 1996, an X310 10 GbE
// frame; 2048, the simulated device; 16384 and 65536, channelizer-sized),
// repeatedly with a fresh demodulator until at least 1 s of thread CPU. With
// two demodulators, two threads on separate physical cores each run their own
// demodulator over their own copy at the same time.
//
// The demodulator sees samples at the channel rate; the waveform (M=48,
// CP=6) does not depend on the sample rate, so throughput in samples per
// second translates directly to headroom at 1, 2 and 5 MS/s. Channelizer,
// MAC and I/O costs are not included here (see the full-stack soak).
//
// Usage: stream_throughput OUT.json CORE [CORE2] [config ...]
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>

#include "phy_bench.hh"

using namespace bench;

namespace {

void pin(int core)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

struct Capture {
    std::vector<fc32> iq;
    unsigned frames = 0;
};

Capture build(PHY &phy, const Config &c, const std::string &pattern, std::mt19937_64 &rng)
{
    const size_t total = 2000000;
    Capture cap;
    std::vector<std::pair<size_t, size_t>> frames;
    auto mod = phy.mkPacketModulator();
    unsigned seq = 0;
    auto frame = [&](std::vector<fc32> &out) {
        ModPacket m;
        mod->modulate(mkPacket(seq++, 1000, rng), 1.0f, m);
        const fc32 *x = m.samples->data() + m.offset;
        frames.push_back({out.size(), m.nsamples});
        out.insert(out.end(), x, x + m.nsamples);
    };
    std::vector<fc32> &s = cap.iq;
    s.reserve(total + 20000);
    s.resize(1000);
    if (pattern == "idle") {
        s.resize(total);
    } else if (pattern == "saturated") {
        while (s.size() < total) { frame(s); s.resize(s.size() + 100); }
    } else {   // bursty: 25 ms of frames, 25 ms quiet, at 1 MS/s
        while (s.size() < total) {
            const size_t slot_end = s.size() + 25000;
            while (s.size() + 8000 < slot_end) frame(s);
            s.resize(slot_end + 25000);
        }
    }
    cap.frames = frames.size();
    // Noise level: SNR relative to frame power (or the strong-link level for idle).
    const double snr = pattern == "bursty-weak" ? c.weak_db : 25.0;
    double p = frame_power(s, frames);
    if (p == 0) {   // idle: use the level a strong frame would have
        std::vector<fc32> one;
        frame(one);
        p = frame_power(one, {{0, one.size()}});
    }
    add_awgn(s, p / std::pow(10.0, snr / 10.0), rng);
    return cap;
}

struct Run { double cpu = 0, wall = 0; size_t samples = 0; unsigned passes = 0, delivered_first = 0; };

Run run(PHY &phy, const Capture &cap, size_t chunk, double min_cpu)
{
    Run r;
    const double c0 = thread_cpu(), w0 = wall();
    for (unsigned pass = 0; pass < 2 || thread_cpu() - c0 < min_cpu; ++pass) {
        auto demod = phy.mkPacketDemodulator();
        unsigned delivered = 0;
        demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) {
            if (p && !p->internal_flags.invalid_payload) ++delivered;
        });
        demod->reset(Channel(0.0, 1e6));
        demod->timestamp(MonoClock::time_point{100.0}, std::nullopt, 0, 0, 1.0f, 1e6f);
        const fc32 *x = cap.iq.data();
        for (size_t i = 0; i < cap.iq.size(); i += chunk)
            demod->demodulate(x + i, std::min(chunk, cap.iq.size() - i));
        if (pass == 0) r.delivered_first = delivered;
        r.samples += cap.iq.size();
        ++r.passes;
    }
    r.cpu = thread_cpu() - c0;
    r.wall = wall() - w0;
    return r;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s OUT.json CORE [CORE2] [config ...]\n", argv[0]);
        return 2;
    }
    const int core = std::atoi(argv[2]);
    // Second core: same NUMA node, different physical core (even CPUs are node 0 here).
    int core2 = argc > 3 && std::isdigit(argv[3][0]) ? std::atoi(argv[3]) : core + 2;
    std::vector<std::string> names;
    for (int i = 3; i < argc; ++i)
        if (!std::isdigit(argv[i][0])) names.push_back(argv[i]);
    if (names.empty()) names = {"native qpsk rs8", "native qpsk uncoded chase4", "liquid qpsk rs8", "liquid qpsk uncoded"};
    PHY::setTeam(0);
    PHY::setNodeId(2);
    pin(core);

    const std::vector<std::string> patterns = {"idle", "bursty", "saturated", "bursty-weak"};
    const std::vector<size_t> chunks = {363, 1996, 2048, 16384, 65536};
    FILE *f = std::fopen(argv[1], "w");
    std::fprintf(f, "{\"core\": %d, \"core2\": %d, \"results\": [\n", core, core2);
    bool first = true;
    for (const auto &name : names) {
        const Config &c = config(name);
        auto phy = mkPHY(c);
        for (const auto &pattern : patterns) {
            std::mt19937_64 rng(7);
            const Capture cap = build(*phy, c, pattern, rng);
            for (size_t chunk : chunks) {
                for (int ndemod : {1, 2}) {
                    std::vector<Run> runs(ndemod);
                    if (ndemod == 1) {
                        runs[0] = run(*phy, cap, chunk, 1.0);
                    } else {
                        std::thread t([&] { pin(core2); runs[1] = run(*phy, cap, chunk, 1.0); });
                        runs[0] = run(*phy, cap, chunk, 1.0);
                        t.join();
                    }
                    double cpu_msps = 1e9, wall_total = 0;
                    size_t samples = 0;
                    for (auto &r : runs) {
                        cpu_msps = std::min(cpu_msps, r.samples / r.cpu / 1e6);
                        wall_total = std::max(wall_total, r.wall);
                        samples += r.samples;
                    }
                    std::fprintf(f, "%s{\"config\": \"%s\", \"pattern\": \"%s\", \"chunk\": %zu, \"demodulators\": %d, "
                                 "\"frames\": %u, \"delivered_first_pass\": %u, \"capture_samples\": %zu, \"passes\": %u, "
                                 "\"msps_per_demod_cpu\": %.3f, \"ns_per_sample\": %.3f, \"aggregate_msps_wall\": %.3f, "
                                 "\"headroom_1msps\": %.2f, \"headroom_2msps\": %.2f, \"headroom_5msps\": %.2f}",
                                 first ? "" : ",\n", name.c_str(), pattern.c_str(), chunk, ndemod, cap.frames,
                                 runs[0].delivered_first, cap.iq.size(), runs[0].passes,
                                 cpu_msps, 1e3 / cpu_msps, samples / wall_total / 1e6,
                                 cpu_msps / 1.0, cpu_msps / 2.0, cpu_msps / 5.0);
                    first = false;
                    std::fflush(f);
                    std::fprintf(stderr, "%-28s %-11s chunk %6zu x%d: %7.2f MS/s per demod (%6.1f ns/sample), delivered %u/%u\n",
                                 name.c_str(), pattern.c_str(), chunk, ndemod, cpu_msps, 1e3 / cpu_msps,
                                 runs[0].delivered_first, cap.frames);
                }
            }
        }
    }
    std::fprintf(f, "\n]}\n");
    std::fclose(f);
    return 0;
}
