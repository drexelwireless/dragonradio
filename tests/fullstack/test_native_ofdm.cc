// Production-path test of the native OFDM PHY (P1). Packets travel through the
// real NetPacket -> PHY::PacketModulator -> IQ -> PHY::PacketDemodulator ->
// RadioPacket classes, with the production team/next-hop filter, timestamps
// and MCS table. liquid OFDM runs through the same harness as a control.
// No radio is opened: MonoClock falls back to the host clock without a USRP.
#include <cmath>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Packet.hh"
#include "phy/NativeOFDM.hh"
#include "phy/OFDM.hh"

using fc32 = std::complex<float>;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct Spec {
    NodeId cur, next;
    uint16_t seq;
    size_t datalen;
    mcsidx_t mcs;
    uint8_t team;
    bool wanted;
};

static std::shared_ptr<NetPacket> mkPacket(const Spec &s, unsigned salt)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + s.datalen);
    pkt->hdr.curhop = s.cur;
    pkt->hdr.nexthop = s.next;
    pkt->hdr.seq = Seq{s.seq};
    pkt->hdr.flags.syn = s.seq == 0;
    pkt->hdr.flags.ack = s.seq % 3 == 0;
    pkt->hdr.flags.has_seq = 1;
    pkt->hdr.flags.has_control = 0;
    pkt->hdr.flags.compressed = 0;
    pkt->hdr.flags.team = 0;
    pkt->ehdr().src = s.cur;
    pkt->ehdr().dest = s.next;
    pkt->ehdr().ack = Seq{static_cast<uint16_t>(s.seq ^ 0x5a5a)};
    pkt->ehdr().data_len = static_cast<uint16_t>(s.datalen);
    unsigned char *d = pkt->data() + sizeof(ExtendedHeader);
    for (size_t i = 0; i < s.datalen; ++i)
        d[i] = static_cast<unsigned char>((i * 131 + s.seq * 7 + salt) & 0xff);
    // Every fourth packet also carries MAC control messages after its data
    // (NAK and selective ACK), as the ARQ controller produces.
    if (s.seq % 4 == 1) {
        pkt->appendNak(Seq{static_cast<uint16_t>(s.seq - 1)});
        pkt->appendSelectiveAck(Seq{static_cast<uint16_t>(s.seq - 9)}, Seq{static_cast<uint16_t>(s.seq - 2)});
    }
    pkt->mcsidx = s.mcs;
    return pkt;
}

struct Expected {
    Spec spec;
    bool control;
    std::vector<unsigned char> body;
    size_t offset;    // frame start within the IQ stream
    size_t samples;
};

static std::vector<Spec> specs()
{
    std::vector<Spec> v;
    // Unicast to node 2 across sequence wraparound, every size class, both MCS.
    const size_t sizes[] = {0, 1, 17, 64, 255, 256, 600, 1024, 1500};
    for (unsigned i = 0; i < 27; ++i)
        v.push_back({1, 2, static_cast<uint16_t>(65520 + i), sizes[i % 9], i % 2, 2, true});
    v.push_back({1, 3, 7, 300, 0, 2, false});      // other next hop: skipped
    v.push_back({1, 255, 8, 200, 1, 2, true});     // broadcast: delivered
    v.push_back({2, 255, 9, 100, 0, 2, false});    // our own transmission: dropped
    v.push_back({1, 2, 10, 120, 0, 5, false});     // other team: dropped
    v.push_back({4, 2, 11, 1500, 1, 2, true});     // different sender, MTU payload
    return v;
}

struct Run {
    std::vector<fc32> iq;
    std::vector<Expected> expected;
};

static Run modulateAll(PHY &phy, const std::vector<Spec> &list, bool check_sizes)
{
    Run run;
    auto mod = phy.mkPacketModulator();
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0, 1e-3f);
    auto gap = [&](size_t n) { for (size_t i = 0; i < n; ++i) run.iq.emplace_back(noise(rng), noise(rng)); };
    gap(2500);
    unsigned salt = 0;
    for (const Spec &s : list) {
        PHY::setTeam(s.team);
        auto pkt = mkPacket(s, ++salt);
        std::vector<unsigned char> body(pkt->data(), pkt->data() + pkt->size());
        const bool control = pkt->hdr.flags.has_control;
        ModPacket mpkt;
        mod->modulate(pkt, 1.0f, mpkt);
        // getModulatedSize(mcs, n) counts n bytes after the extended header,
        // which here includes any control messages.
        const size_t n = body.size() - sizeof(ExtendedHeader);
        if (check_sizes)
            CHECK(phy.getModulatedSize(s.mcs, n) == mpkt.nsamples,
                  "getModulatedSize %zu != %zu (len %zu mcs %u)", phy.getModulatedSize(s.mcs, n), mpkt.nsamples, n, s.mcs);
        Expected e{s, control, body, run.iq.size(), mpkt.nsamples};
        const fc32 *x = mpkt.samples->data() + mpkt.offset;
        run.iq.insert(run.iq.end(), x, x + mpkt.nsamples);
        run.expected.push_back(e);
        gap(100 + (salt * 37) % 400);
    }
    PHY::setTeam(2);
    gap(2500);
    return run;
}

static void impair(std::vector<fc32> &iq, double cfo_subcarriers, unsigned M, double snr_db)
{
    std::mt19937 rng(11);
    double power = 0;
    for (const auto &z : iq) power += std::norm(z);
    power /= iq.size();
    std::normal_distribution<float> noise(0, static_cast<float>(std::sqrt(power * std::pow(10, -snr_db / 10) / 2)));
    const double w = 2 * M_PI * cfo_subcarriers / M;
    for (size_t n = 0; n < iq.size(); ++n)
        iq[n] = iq[n] * std::polar(1.0f, static_cast<float>(w * n)) + fc32(noise(rng), noise(rng));
}

static std::vector<std::shared_ptr<RadioPacket>> demodulateAll(PHY &phy, const std::vector<fc32> &iq, size_t chunk)
{
    std::vector<std::shared_ptr<RadioPacket>> got;
    auto demod = phy.mkPacketDemodulator();
    demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) { got.push_back(std::move(p)); });
    demod->reset(Channel(0.0, 1e6));
    demod->timestamp(MonoClock::time_point{100.0}, std::nullopt, 0, 0, 1.0f, 1e6f);
    for (size_t i = 0; i < iq.size(); i += chunk)
        demod->demodulate(iq.data() + i, std::min(chunk, iq.size() - i));
    return got;
}

static void verify(const char *name, const Run &run, const std::vector<std::shared_ptr<RadioPacket>> &got, int timing_tolerance)
{
    std::vector<const Expected *> wanted;
    for (const auto &e : run.expected) if (e.spec.wanted) wanted.push_back(&e);
    CHECK(got.size() == wanted.size(), "%s: delivered %zu packets, expected %zu", name, got.size(), wanted.size());
    for (size_t i = 0; i < std::min(got.size(), wanted.size()); ++i) {
        const auto &p = *got[i];
        const Expected &e = *wanted[i];
        CHECK(p.hdr.curhop == e.spec.cur && p.hdr.nexthop == e.spec.next, "%s[%zu]: hops %u->%u", name, i, p.hdr.curhop, p.hdr.nexthop);
        CHECK(static_cast<uint16_t>(p.hdr.seq) == e.spec.seq, "%s[%zu]: seq %u != %u", name, i, (unsigned)static_cast<uint16_t>(p.hdr.seq), e.spec.seq);
        CHECK(p.hdr.flags.team == e.spec.team && p.hdr.flags.has_seq == 1 && p.hdr.flags.syn == (e.spec.seq == 0) && p.hdr.flags.ack == (e.spec.seq % 3 == 0),
              "%s[%zu]: flags", name, i);
        CHECK(!p.internal_flags.invalid_payload && p.integrityIntact(), "%s[%zu]: payload invalid", name, i);
        CHECK(p.hdr.flags.has_control == e.control && (!e.control || p.getControlLen() > 0), "%s[%zu]: control messages", name, i);
        CHECK(p.size() == e.body.size() && std::memcmp(p.data(), e.body.data(), e.body.size()) == 0, "%s[%zu]: payload bytes differ", name, i);
        CHECK(p.ehdr().data_len == e.spec.datalen && p.ehdr().src == e.spec.cur && p.ehdr().dest == e.spec.next, "%s[%zu]: extended header", name, i);
        CHECK(p.mcsidx == e.spec.mcs, "%s[%zu]: mcs %u != %u", name, i, p.mcsidx, e.spec.mcs);
        const long long dstart = static_cast<long long>(p.start_samples) - static_cast<long long>(e.offset);
        CHECK(std::llabs(dstart) <= timing_tolerance, "%s[%zu]: start %zu vs %zu", name, i, p.start_samples, e.offset);
        CHECK(std::fabs((p.timestamp - MonoClock::time_point{100.0}).get_real_secs() - p.start_samples / 1e6) < 1e-9, "%s[%zu]: timestamp", name, i);
        CHECK(p.channel == Channel(0.0, 1e6) && p.bw == 1e6f, "%s[%zu]: channel", name, i);
    }
}


// Drive the demodulator the way TDChannelizer and FDChannelizer do: one
// timestamp() per contiguous receive buffer, preceded by reset() unless the
// demodulator reports isFrameOpen(). Buffer boundaries are cut at varying
// distances after each frame start, where an unscanned preamble straddles
// the boundary (receive buffers end up to one recv chunk past a slot
// boundary, and TDMA frames start at slot boundaries).
static size_t channelizerProtocol(const char *name, PHY &phy, const Run &run, bool check)
{
    std::vector<std::shared_ptr<RadioPacket>> got;
    auto demod = phy.mkPacketDemodulator();
    demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) { got.push_back(std::move(p)); });
    std::vector<size_t> cuts;
    for (size_t i = 0; i < run.expected.size(); ++i)
        cuts.push_back(run.expected[i].offset + (i * 13) % 240);
    size_t start = 0;
    for (size_t b = 0; b <= cuts.size(); ++b) {
        size_t end = b < cuts.size() ? cuts[b] : run.iq.size();
        if (!demod->isFrameOpen())
            demod->reset(Channel(0.0, 1e6));
        demod->timestamp(MonoClock::time_point{100.0 + start / 1e6}, std::nullopt, 0, 0, 1.0f, 1e6f);
        demod->demodulate(run.iq.data() + start, end - start);
        start = end;
    }
    std::vector<const Expected *> wanted;
    for (const auto &e : run.expected) if (e.spec.wanted) wanted.push_back(&e);
    if (check) {
        CHECK(got.size() == wanted.size(), "%s: delivered %zu packets across buffer boundaries, expected %zu", name, got.size(), wanted.size());
        for (size_t i = 0; i < std::min(got.size(), wanted.size()); ++i) {
            const auto &p = *got[i];
            const Expected &e = *wanted[i];
            CHECK(static_cast<uint16_t>(p.hdr.seq) == e.spec.seq && p.size() == e.body.size()
                  && std::memcmp(p.data(), e.body.data(), e.body.size()) == 0, "%s[%zu]: packet differs", name, i);
            CHECK(std::fabs((p.timestamp - MonoClock::time_point{100.0}).get_real_secs() - e.offset / 1e6) < 1e-9,
                  "%s[%zu]: absolute timestamp across buffers", name, i);
        }
    }
    return got.size();
}

static void scenario(const char *name, PHY &phy, bool exact_sizes, int timing_tolerance, double cfo, unsigned M)
{
    PHY::setNodeId(2);
    Run run = modulateAll(phy, specs(), exact_sizes);
    for (size_t chunk : {size_t(97), size_t(4096), run.iq.size()}) {
        auto got = demodulateAll(phy, run.iq, chunk);
        verify(name, run, got, timing_tolerance);
    }
    Run noisy = run;
    impair(noisy.iq, cfo, M, 25.0);
    verify((std::string(name) + " +CFO/noise").c_str(), noisy, demodulateAll(phy, noisy.iq, 4096), timing_tolerance + 2);
    if (exact_sizes)
        channelizerProtocol((std::string(name) + " buffers").c_str(), phy, run, true);
    else {
        size_t wanted = 0;
        for (const auto &e : run.expected) wanted += e.spec.wanted;
        std::printf("%-34s %zu of %zu delivered with boundaries just after frame starts (informational)\n",
                    name, channelizerProtocol(name, phy, run, false), wanted);
    }
    size_t controls = 0;
    for (const auto &e : run.expected) controls += e.control;
    std::printf("%-34s %zu frames (%zu with control), %zu samples: checked\n", name, run.expected.size(), controls, run.iq.size());
}

static void crash(int sig)
{
    void *frames[64];
    int n = backtrace(frames, 64);
    std::fprintf(stderr, "signal %d; backtrace:\n", sig);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

int main()
{
    std::signal(SIGSEGV, crash);
    std::signal(SIGABRT, crash);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    PHY::setTeam(2);
    const liquid::MCS qpsk(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    const liquid::MCS qam16(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QAM16);
    std::vector<std::pair<liquid::MCS, AutoGain>> table = {{qpsk, AutoGain()}, {qam16, AutoGain()}};

    // Production default layout (M = 48, CP = 6, liquid default mask) and a
    // 128-tone layout; liquid OFDM is the control through the same harness.
    NativeOFDM native48(table, 48, 6, std::nullopt);
    NativeOFDM native128(table, 128, 16, std::nullopt);
    liquid::OFDM legacy48(qpsk, table, false, false, 48, 6, 4, std::nullopt);
    std::printf("native M=48 mask %s\n", native48.getSubcarriers().c_str());

    scenario("native-ofdm M=48 CP=6", native48, true, 0, 0.6, 48);
    scenario("native-ofdm M=128 CP=16", native128, true, 0, 1.0, 128);
    scenario("liquid ofdm M=48 CP=6 (control)", legacy48, false, 64, 0.3, 48);

    // Unsupported MCS entries are rejected, not silently replaced.
    for (auto fec : {LIQUID_FEC_RS_M8, LIQUID_FEC_CONV_V27}) {
        bool threw = false;
        try {
            NativeOFDM bad({{liquid::MCS(LIQUID_CRC_32, LIQUID_FEC_NONE, fec, LIQUID_MODEM_QPSK), AutoGain()}}, 48, 6, std::nullopt);
        } catch (const std::invalid_argument &) { threw = true; }
        CHECK(threw, "unsupported FEC %d accepted", int(fec));
    }
    {
        bool threw = false;
        try { NativeOFDM bad({{liquid::MCS(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QAM64), AutoGain()}}, 48, 6, std::nullopt); }
        catch (const std::invalid_argument &) { threw = true; }
        CHECK(threw, "unsupported constellation accepted");
    }

    // Two demodulators on separate threads (one per channel in production).
    {
        PHY::setNodeId(2);
        Run run = modulateAll(native48, specs(), false);
        std::vector<std::shared_ptr<RadioPacket>> a, b;
        std::thread ta([&] { a = demodulateAll(native48, run.iq, 1024); });
        std::thread tb([&] { b = demodulateAll(native48, run.iq, 333); });
        ta.join(); tb.join();
        verify("native-ofdm thread A", run, a, 0);
        verify("native-ofdm thread B", run, b, 0);
        std::printf("%-34s two concurrent demodulators: checked\n", "native-ofdm threads");
    }

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
