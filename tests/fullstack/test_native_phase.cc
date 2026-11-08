// Receive phase wander, native OFDM against liquid OFDM (the control) through
// the production modulator and demodulator. Each receiver sees the frames with
// an oscillator phase added: a Wiener process of a given linewidth, or a
// sinusoid. Printed as delivered frames per condition.
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <functional>
#include <random>
#include <vector>

#include "Packet.hh"
#include "phy/NativeOFDM.hh"
#include "phy/OFDM.hh"

using fc32 = std::complex<float>;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static constexpr double kFs = 1e6;
static constexpr unsigned kFrames = 40;

static std::shared_ptr<NetPacket> mkPacket(uint16_t seq, size_t len)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader) + len);

    pkt->hdr.curhop = 1;
    pkt->hdr.nexthop = 2;
    pkt->hdr.seq = Seq{seq};
    pkt->hdr.flags.syn = 0;
    pkt->hdr.flags.ack = 0;
    pkt->hdr.flags.has_seq = 1;
    pkt->hdr.flags.has_control = 0;
    pkt->hdr.flags.compressed = 0;
    pkt->hdr.flags.team = 0;
    pkt->ehdr().src = 1;
    pkt->ehdr().dest = 2;
    pkt->ehdr().ack = Seq{0};
    pkt->ehdr().data_len = static_cast<uint16_t>(len);
    unsigned char *d = pkt->data() + sizeof(ExtendedHeader);
    for (size_t i = 0; i < len; ++i)
        d[i] = static_cast<unsigned char>((i*131 + seq*7) & 0xff);
    pkt->mcsidx = 0;
    return pkt;
}

// A stream of frames with gaps, unit-ish power in the frames.
static std::vector<fc32> frames(PHY &phy, size_t len)
{
    std::vector<fc32> iq(2500);
    auto mod = phy.mkPacketModulator();

    PHY::setTeam(2);
    PHY::setNodeId(2);
    for (unsigned i = 0; i < kFrames; ++i) {
        auto    pkt = mkPacket(i + 1, len);
        ModPacket mpkt;

        mod->modulate(pkt, 1.0f, mpkt);
        const fc32 *x = mpkt.samples->data() + mpkt.offset;
        iq.insert(iq.end(), x, x + mpkt.nsamples);
        iq.resize(iq.size() + 300);
    }
    iq.resize(iq.size() + 2500);
    return iq;
}

using Phase = std::function<double(size_t)>;

static Phase wiener(double linewidth_hz, unsigned seed)
{
    auto   rng = std::make_shared<std::mt19937>(seed);
    auto   n = std::make_shared<std::normal_distribution<double>>(0.0, std::sqrt(2*M_PI*linewidth_hz/kFs));
    auto   phi = std::make_shared<double>(0.0);

    return [=](size_t) { return *phi += (*n)(*rng); };
}

static Phase sinusoid(double rad, double hz)
{
    return [=](size_t k) { return rad*std::sin(2*M_PI*hz*k/kFs); };
}

// Fractional delay d (samples) as a windowed sinc, centred on lag 0.
static std::vector<float> fracDelay(double d)
{
    std::vector<float> h(33);

    for (int i = 0; i < 33; ++i) {
        const double t = i - 16 - d;
        const double w = 0.5*(1 + std::cos(M_PI*(i - 16)/17.0));

        h[i] = static_cast<float>((t == 0 ? 1.0 : std::sin(M_PI*t)/(M_PI*t))*w);
    }
    return h;
}

// y[k] = sum h[i] x[k - i + centre]
static std::vector<fc32> filter(const std::vector<fc32> &x, const std::vector<float> &h, int centre)
{
    std::vector<fc32> y(x.size());

    for (size_t k = 0; k < x.size(); ++k)
        for (size_t i = 0; i < h.size(); ++i) {
            const long j = static_cast<long>(k) - static_cast<long>(i) + centre;

            if (j >= 0 && j < static_cast<long>(x.size()))
                y[k] += h[i]*x[j];
        }
    return y;
}

static double median_evm = 0;

static unsigned delivered(PHY &phy, const std::vector<fc32> &clean, double snr_db, const Phase &phase, unsigned seed)
{
    std::vector<fc32> iq = clean;
    double            power = 0;
    size_t            active = 0;

    for (const fc32 &z : iq)
        if (std::norm(z) > 0) {
            power += std::norm(z);
            ++active;
        }
    power /= active;

    std::mt19937                    rng(seed);
    std::normal_distribution<float> noise(0, static_cast<float>(std::sqrt(power*std::pow(10, -snr_db/10)/2)));

    for (size_t k = 0; k < iq.size(); ++k)
        iq[k] = iq[k]*std::polar(1.0f, static_cast<float>(phase(k))) + fc32(noise(rng), noise(rng));

    unsigned ok = 0;
    auto     demod = phy.mkPacketDemodulator(0, Channel(0, 1e6));
    std::vector<float> evms;

    demod->setCallback([&](std::shared_ptr<RadioPacket> &&p) {
        if (!p->internal_flags.invalid_header)
            evms.push_back(p->evm);
        if (!p->internal_flags.invalid_payload && !p->internal_flags.invalid_header)
            ++ok;
    });
    demod->reset(Channel(0.0, 1e6));
    demod->timestamp(MonoClock::time_point{MonoClock::duration(100.0)}, std::nullopt, 0, 0, 1.0f, 1e6f);
    for (size_t i = 0; i < iq.size(); i += 4096)
        demod->demodulate(iq.data() + i, std::min<size_t>(4096, iq.size() - i));
    demod.reset();
    std::sort(evms.begin(), evms.end());
    median_evm = evms.empty() ? 0.0 : evms[evms.size()/2];
    return ok;
}

// Timing and channel shape: every link's frames arrive at some fractional
// sample offset, and a receiver filter can put energy ahead of the strongest
// path, as at grid57 (2026-11-06: taps 0.78, 1 at lags -1, 0, a 9 dB droop to
// the band edge). The FFT window and channel model must hold both.
static void timing(NativeOFDM &native, liquid::OFDM &legacy)
{
    const auto clean_n = frames(native, 1048), clean_l = frames(legacy, 1048);
    const Phase none = [](size_t) { return 0.0; };

    std::printf("\n1048-byte frames, SNR 30 dB: native delivered / EVM, liquid delivered\n");
    for (double d : {0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875}) {
        auto           h = fracDelay(d);
        const unsigned n = delivered(native, filter(clean_n, h, 16), 30, none, 3);
        const double   evm = median_evm;
        const unsigned l = delivered(legacy, filter(clean_l, h, 16), 30, none, 3);

        std::printf("  delay %.3f samples         native %3u %6.1f dB  liquid %3u\n", d, n, evm, l);
        CHECK(n == kFrames, "native at fractional delay %.3f: %u of %u", d, n, kFrames);
        CHECK(evm < -17, "native EVM at fractional delay %.3f: %.1f dB", d, evm);
    }
    for (const auto &[name, h, centre] : {std::tuple<const char *, std::vector<float>, int>{"early tap, -2.2 dB", {0.78f, 1.0f}, 1},
                                          {"late tap, -2.2 dB", {1.0f, 0.78f}, 0},
                                          {"equal taps", {1.0f, 1.0f}, 1},
                                          {"taps -2,-1,0,1,2 of grid57", {0.18f, 0.78f, 1.0f, 0.17f, 0.15f}, 2}}) {
        const unsigned n = delivered(native, filter(clean_n, h, centre), 30, none, 3);
        const double   evm = median_evm;
        const unsigned l = delivered(legacy, filter(clean_l, h, centre), 30, none, 3);

        std::printf("  %-28s native %3u %6.1f dB  liquid %3u\n", name, n, evm, l);
        CHECK(n == kFrames, "native with %s: %u of %u", name, n, kFrames);
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    PHY::setTeam(2);
    PHY::setNodeId(2);

    // The over-the-air configuration: QPSK with rs8 on the payload.
    const liquid::MCS rs8(LIQUID_CRC_32, LIQUID_FEC_RS_M8, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK);
    const liquid::MCS header(LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_CONV_V29P78, LIQUID_MODEM_BPSK);
    std::vector<std::pair<liquid::MCS, AutoGain>> table = {{rs8, AutoGain()}};

    NativeOFDM   native(table, 48, 6, std::nullopt, 4096, 4, true, "v29");
    liquid::OFDM legacy(header, table, false, true, 48, 6, 4, std::nullopt);

    timing(native, legacy);

    // Near the coding threshold, no phase noise: what the receiver spends on noise.
    {
        const auto clean_n = frames(native, 1048), clean_l = frames(legacy, 1048);
        const Phase none = [](size_t) { return 0.0; };

        std::printf("\n1048-byte frames, no phase noise (delivered of %u, four noise seeds each)\n", 4*kFrames);
        for (double snr : {6.0, 7.0, 8.0, 9.0, 10.0, 11.0}) {
            unsigned n = 0, l = 0;

            for (unsigned seed : {21u, 22u, 23u, 24u}) {
                n += delivered(native, clean_n, snr, none, seed);
                l += delivered(legacy, clean_l, snr, none, seed);
            }
            std::printf("  SNR %4.1f dB                  native %3u  liquid %3u\n", snr, n, l);
        }
    }

    struct Case { const char *name; std::function<Phase(unsigned)> make; };
    std::vector<Case> cases = {{"none", [](unsigned) { return Phase([](size_t) { return 0.0; }); }}};

    for (double lw : {3.0, 10.0, 30.0, 100.0, 300.0, 1000.0})
        cases.push_back({nullptr, [lw](unsigned s) { return wiener(lw, s); }});

    const std::vector<std::pair<double, double>> tones = {{0.5, 200.0}, {0.5, 1000.0}, {0.3, 3000.0}, {0.3, 10000.0}};

    for (auto [rad, hz] : tones)
        cases.push_back({nullptr, [rad = rad, hz = hz](unsigned) { return sinusoid(rad, hz); }});

    // A steady carrier offset of either sign, as between two free-running radios.
    const std::vector<double> offsets = {-2000, -800, -400, -300, -200, -100, 100, 200, 300, 400, 800, 2000};

    for (double hz : offsets)
        cases.push_back({nullptr, [hz](unsigned) { return Phase([hz](size_t k) { return 2*M_PI*hz*k/kFs; }); }});

    auto label = [&](size_t i) -> std::string {
        char b[64];

        if (i == 0)
            return "none";
        if (i < 7) {
            const double lw[] = {3, 10, 30, 100, 300, 1000};
            std::snprintf(b, sizeof b, "wiener %.0f Hz", lw[i - 1]);
        } else if (i < 7 + tones.size())
            std::snprintf(b, sizeof b, "sine %.1f rad %.0f Hz", tones[i - 7].first, tones[i - 7].second);
        else
            std::snprintf(b, sizeof b, "carrier offset %+.0f Hz", offsets[i - 7 - tones.size()]);
        return b;
    };

    for (size_t len : {size_t(100), size_t(548), size_t(1048)}) {
        const auto clean_n = frames(native, len), clean_l = frames(legacy, len);

        for (double snr : {25.0, 13.0}) {
            std::printf("\n%zu-byte frames, SNR %.0f dB (delivered of %u)\n", len, snr, kFrames);
            for (size_t i = 0; i < cases.size(); ++i) {
                const unsigned n = delivered(native, clean_n, snr, cases[i].make(1), 5);
                const unsigned l = delivered(legacy, clean_l, snr, cases[i].make(1), 5);

                std::printf("  %-26s native %3u  liquid %3u\n", label(i).c_str(), n, l);
                if (i == 0 && snr == 25.0) {
                    CHECK(n == kFrames, "native clean %u", n);
                    CHECK(l == kFrames, "liquid clean %u", l);
                }
            }
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
