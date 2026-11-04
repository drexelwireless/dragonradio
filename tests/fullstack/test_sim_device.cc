// Tests of the simulated device and synthetic traffic used by full-stack
// simulation (P2). A minimal relay inside the test forwards bursts between two
// SimUSRPs exactly as scripts/sim-air.py does, so sample placement, link
// impairments and UHD-like timing behavior can be checked sample by sample.
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <list>
#include <thread>
#include <vector>

#include "Clock.hh"
#include "sim/SimTraffic.hh"
#include "sim/SimUSRP.hh"

using fc32 = std::complex<float>;
using sim::SimUSRP;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

// Forward every message from one client to the other.
class Relay {
public:
    explicit Relay(const std::string &path) : path_(path), done_(false)
    {
        unlink(path.c_str());
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un a = {};
        a.sun_family = AF_UNIX;
        strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(fd_, 4) != 0)
            throw std::runtime_error("relay bind");
        thread_ = std::thread([this]{ run(); });
    }

    ~Relay()
    {
        done_ = true;
        shutdown(fd_, SHUT_RDWR);
        close(fd_);
        for (int c : clients_)
            shutdown(c, SHUT_RDWR);
        thread_.join();
        for (int c : clients_)
            close(c);
        unlink(path_.c_str());
    }

private:
    std::string path_;
    int fd_;
    std::atomic<bool> done_;
    std::vector<int> clients_;
    std::thread thread_;

    static bool readFull(int fd, void *p, size_t n)
    {
        char *c = static_cast<char*>(p);
        while (n) {
            ssize_t r = read(fd, c, n);
            if (r <= 0)
                return false;
            c += r;
            n -= r;
        }
        return true;
    }

    void run()
    {
        while (clients_.size() < 2) {
            int c = accept(fd_, nullptr, nullptr);
            if (c < 0)
                return;
            clients_.push_back(c);
        }
        while (!done_) {
            struct pollfd p[2] = {{clients_[0], POLLIN, 0}, {clients_[1], POLLIN, 0}};
            if (poll(p, 2, 100) <= 0)
                continue;
            for (int i = 0; i < 2; ++i) {
                if (!(p[i].revents & POLLIN))
                    continue;
                sim::AirHeader h;
                if (!readFull(clients_[i], &h, sizeof(h)))
                    return;
                std::vector<char> x(h.nsamples * sizeof(fc32));
                if (!x.empty() && !readFull(clients_[i], x.data(), x.size()))
                    return;
                if (h.type != sim::kAirBurst)
                    continue;
                int o = clients_[1 - i];
                (void) !write(o, &h, sizeof(h));
                for (size_t off = 0; off < x.size(); ) {
                    ssize_t w = write(o, x.data() + off, x.size() - off);
                    if (w <= 0)
                        return;
                    off += w;
                }
            }
        }
    }
};

static std::list<std::shared_ptr<IQBuf>> burst(size_t n, size_t delay = 0)
{
    auto b = std::make_shared<IQBuf>(n + delay);
    for (size_t i = 0; i < delay; ++i)
        (*b)[i] = fc32(9, 9);  // must not be transmitted
    for (size_t i = 0; i < n; ++i)
        (*b)[delay + i] = std::polar(0.5f + 0.4f * std::sin(0.01f * i), 0.003f * i * i);
    b->delay = delay;
    return {b};
}

// Receive [t0, t0+n) on rx after transmitting from tx at t0+offset/fs.
static std::vector<fc32> exchange(SimUSRP &tx, SimUSRP &rx, size_t offset, size_t nsamps,
                                  std::list<std::shared_ptr<IQBuf>> bufs, IQBuf *meta = nullptr)
{
    const double fs = rx.getRXRate();
    MonoClock::time_point now = MonoClock::now();
    // Align to a whole number of samples 60 ms in the future.
    MonoClock::time_point t0 { uhd::time_spec_t::from_ticks((now + 0.06).t.to_ticks(fs), fs) };
    rx.startRXStream(t0);
    tx.burstTX(t0 + offset / fs, true, true, bufs);
    IQBuf buf(rx.getRecommendedBurstRXSize(nsamps));
    bool ok = rx.burstRX(t0, nsamps, buf);
    CHECK(ok, "burstRX returned false");
    CHECK(buf.timestamp && approx(*buf.timestamp, t0), "first sample timestamp");
    CHECK(buf.undersample == 0, "undersample %zu", buf.undersample);
    CHECK(buf.size() >= nsamps && buf.oversample == buf.size() - nsamps, "oversample bookkeeping");
    rx.stopRXStream();
    if (meta) {
        meta->undersample = buf.undersample;
        meta->oversample = buf.oversample;
    }
    return std::vector<fc32>(buf.data(), buf.data() + nsamps);
}

int main()
{
    std::string path = "/tmp/sim-device-test-" + std::to_string(getpid()) + ".sock";
    Relay relay(path);
    SimUSRP a(path, 1, 1e9, 200e6, 2e-3, 1);
    SimUSRP b(path, 2, 1e9, 200e6, 2e-3, 2);
    const double fs = 1e6;
    for (SimUSRP *u : {&a, &b}) {
        u->setTXRate(fs);
        u->setRXRate(fs);
        u->setMaxRXSampsFactor(2);
        u->setNoisePower(-300.0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto ref = burst(3000);
    const fc32 *x = (*ref.front()).data();

    // 1. Placement, gain and delay are exact; the IQBuf delay prefix is not sent.
    {
        sim::Link l;
        l.gain_db = 20 * std::log10(0.5);
        l.delay = 7;
        b.setLink(1, l);
        auto y = exchange(a, b, 10000, 20000, burst(3000, 13));
        double err = 0, outside = 0;
        for (size_t i = 0; i < y.size(); ++i) {
            long k = static_cast<long>(i) - 10007;
            if (k >= 0 && k < 3000)
                err = std::max(err, (double) std::abs(y[i] - 0.5f * x[k]));
            else
                outside = std::max(outside, (double) std::abs(y[i]));
        }
        CHECK(err < 1e-5, "placement/gain error %g", err);
        CHECK(outside < 1e-9, "energy outside burst %g", outside);
    }

    // 2. Carrier offset is a continuous rotation exp(j 2 pi f t) in absolute time.
    {
        sim::Link l;
        l.cfo_hz = 1234.5;
        b.setLink(1, l);
        auto y = exchange(a, b, 2000, 8000, burst(3000));
        double worst = 0;
        fc32 r0 = y[2000] / x[0];
        for (size_t k = 1; k < 3000; ++k) {
            fc32 r = y[2000 + k] / x[k];
            double dphi = std::arg(r / r0) - 2 * M_PI * 1234.5 * k / fs;
            dphi = std::remainder(dphi, 2 * M_PI);
            worst = std::max(worst, std::abs(dphi));
        }
        CHECK(worst < 1e-3, "CFO phase error %g rad", worst);
        CHECK(std::abs(std::abs(r0) - 1.0f) < 1e-4, "CFO keeps unit gain");
    }

    // 3. A transmitter tuned 100 kHz above the receiver appears shifted by +100 kHz.
    {
        b.setLink(1, sim::Link());
        a.setTXFrequency(1e9 + 100e3);
        auto y = exchange(a, b, 1000, 6000, burst(3000));
        double worst = 0;
        fc32 r0 = y[1000] / x[0];
        for (size_t k = 1; k < 3000; k += 7) {
            double dphi = std::remainder(std::arg((y[1000 + k] / x[k]) / r0) - 2 * M_PI * 100e3 * k / fs, 2 * M_PI);
            worst = std::max(worst, std::abs(dphi));
        }
        CHECK(worst < 1e-3, "tuning offset phase error %g", worst);
        a.setTXFrequency(1e9);
    }

    // 4. Noise power matches the configured level.
    {
        b.setNoisePower(-20.0);
        auto y = exchange(a, b, 0, 50000, {});
        double p = 0;
        for (auto v : y)
            p += std::norm(v);
        p /= y.size();
        CHECK(std::abs(10 * std::log10(p) + 20.0) < 0.2, "noise power %g dB", 10 * std::log10(p));
        // Gaussian, independent I and Q, white: moments of the rendered noise
        // (generator: xoshiro256** + Box-Muller since P4).
        double mi = 0, mq = 0, vi = 0, vq = 0, ki = 0, iq = 0, lag = 0;
        const double n = y.size();
        for (auto v : y) { mi += v.real(); mq += v.imag(); }
        mi /= n; mq /= n;
        for (size_t k = 0; k < y.size(); ++k) {
            const double a_ = y[k].real() - mi, b_ = y[k].imag() - mq;
            vi += a_ * a_; vq += b_ * b_; ki += a_ * a_ * a_ * a_; iq += a_ * b_;
            if (k) lag += a_ * (y[k - 1].real() - mi);
        }
        vi /= n; vq /= n; ki = ki / n / (vi * vi); iq /= n * std::sqrt(vi * vq); lag /= n * vi;
        const double sd = std::sqrt(vi);
        CHECK(std::abs(mi) < 5 * sd / std::sqrt(n) && std::abs(mq) < 5 * sd / std::sqrt(n), "noise mean %g %g", mi, mq);
        CHECK(std::abs(vi / vq - 1) < 0.05, "I/Q variance ratio %g", vi / vq);
        CHECK(std::abs(ki - 3) < 0.15, "I kurtosis %g (Gaussian 3)", ki);
        CHECK(std::abs(iq) < 0.03 && std::abs(lag) < 0.03, "I/Q correlation %g, lag-1 correlation %g", iq, lag);
        b.setNoisePower(-300.0);
    }

    // 5. A timed burst submitted after its start time is dropped and counted late.
    {
        auto s0 = a.getStats();
        auto bufs = burst(1000);
        a.burstTX(MonoClock::now() - 1e-3, true, true, bufs);
        auto s1 = a.getStats();
        CHECK(s1.tx_late == s0.tx_late + 1 && s1.tx_bursts == s0.tx_bursts, "late burst dropped");
        CHECK(a.getTXLateCount() == 1, "late count exposed to the MAC");
    }

    // 6. Erasure removes whole bursts at the receiver and is counted.
    {
        sim::Link l;
        l.erasure = 1.0;
        b.setLink(1, l);
        auto s0 = b.getStats();
        auto y = exchange(a, b, 1000, 6000, burst(3000));
        double e = 0;
        for (auto v : y)
            e += std::norm(v);
        CHECK(e < 1e-12, "erased burst heard (energy %g)", e);
        CHECK(b.getStats().rx_erased == s0.rx_erased + 1, "erasure counted");
        b.setLink(1, sim::Link());
    }

    // 7. Continuation segments follow the first one contiguously, and the
    //    MAC-visible timestamps advance by the transmitted samples.
    {
        MonoClock::time_point now = MonoClock::now();
        MonoClock::time_point t0 { uhd::time_spec_t::from_ticks((now + 0.06).t.to_ticks(fs), fs) };
        b.startRXStream(t0);
        auto first = burst(3000);
        auto second = burst(3000);
        a.burstTX(t0 + 500 / fs, true, false, first);
        CHECK(a.getNextTXTime() && approx(*a.getNextTXTime(), t0 + 3500 / fs), "next TX time");
        a.burstTX(std::nullopt, false, true, second);
        IQBuf buf(b.getRecommendedBurstRXSize(8000));
        b.burstRX(t0, 8000, buf);
        b.stopRXStream();
        double err = 0;
        for (size_t k = 0; k < 3000; ++k)
            err = std::max(err, (double) std::abs(buf[500 + k] - x[k]) + std::abs(buf[3500 + k] - x[k]));
        CHECK(err < 1e-5, "continuation placement %g", err);
        CHECK(approx(*first.front()->timestamp, t0 + 500 / fs), "IQBuf timestamp set");
    }

    // 8. Per-burst jitter delays each burst by a whole number of samples in
    //    [0, jitter], drawn independently per burst.
    {
        sim::Link l;
        l.jitter = 50;
        b.setLink(1, l);
        std::vector<long> offsets;
        for (int trial = 0; trial < 6; ++trial) {
            auto y = exchange(a, b, 1000, 6000, burst(3000));
            long first = -1;
            for (size_t i = 0; i < y.size() && first < 0; ++i)
                if (std::abs(y[i]) > 1e-3)
                    first = static_cast<long>(i);
            offsets.push_back(first - 1000);
            double err = 0;
            for (size_t k = 0; first >= 0 && k < 3000; ++k)
                err = std::max(err, (double) std::abs(y[first + k] - x[k]));
            CHECK(first >= 1000 && first <= 1050 && err < 1e-5, "jittered burst at %ld (err %g)", first, err);
        }
        bool varied = false;
        for (long o : offsets)
            varied |= o != offsets[0];
        CHECK(varied, "jitter did not vary between bursts");

        // Two buffers of one TX burst move together: the second still
        // follows the first with no gap or overlap.
        std::list<std::shared_ptr<IQBuf>> two = burst(3000);
        two.splice(two.end(), burst(3000));
        auto y = exchange(a, b, 1000, 8000, two);
        long first = -1;
        for (size_t i = 0; i < y.size() && first < 0; ++i)
            if (std::abs(y[i]) > 1e-3)
                first = static_cast<long>(i);
        double err = 0;
        for (size_t k = 0; first >= 0 && k < 3000; ++k)
            err = std::max(err, (double) std::abs(y[first + k] - x[k]) + std::abs(y[first + 3000 + k] - x[k]));
        CHECK(first >= 1000 && first <= 1050 && err < 1e-5, "burst buffers jittered apart (first %ld, err %g)", first, err);
        b.setLink(1, sim::Link());
    }

    // 10. Sample clock offset: a tone burst arrives time-warped by (1 + ppm),
    // checked against the analytic tone at an exaggerated offset (2000 ppm,
    // measurable over 4000 samples) and a realistic one (20 ppm).
    for (double ppm : {2000.0, -2000.0, 20.0, -20.0}) {
        const size_t n = 4000;
        const double f0 = 0.05, r = 1 + 1e-6 * ppm;
        auto tone = std::make_shared<IQBuf>(n);
        for (size_t k = 0; k < n; ++k)
            (*tone)[k] = std::polar(0.5f, static_cast<float>(2 * M_PI * f0 * k));
        sim::Link l;
        l.sfo_ppm = ppm;
        b.setLink(1, l);
        auto y = exchange(a, b, 1000, 8000, {tone});
        double err = 0;
        const size_t m = static_cast<size_t>(std::floor((n - 1) / r)) + 1;
        for (size_t k = 40; k + 40 < m; ++k)
            err = std::max(err, std::abs(std::complex<double>(y[1000 + k]) - std::polar(0.5, 2 * M_PI * f0 * k * r)));
        CHECK(err < 2e-3, "SFO %g ppm: interpolation error %g", ppm, err);
        // Nothing beyond the compressed (or stretched) burst.
        double after = 0;
        for (size_t k = m + 20; k < m + 200; ++k)
            after = std::max(after, (double) std::abs(y[1000 + k]));
        CHECK(after < 1e-2, "SFO %g ppm: energy after the burst %g", ppm, after);
        b.setLink(1, sim::Link());
    }

    // 11. IQ imbalance: a tone exp(j w n) gains an image at -w with power
    // ratio |nu|^2/|mu|^2, mu = (1 + a e^{j phi})/2, nu = (1 - a e^{-j phi})/2.
    {
        sim::Impairments imp;
        imp.iq_amp_db = 1.0;
        imp.iq_phase_deg = 5.0;
        b.setImpairments(imp);
        const size_t n = 4000;
        auto tone = std::make_shared<IQBuf>(n);
        for (size_t k = 0; k < n; ++k)
            (*tone)[k] = std::polar(0.5f, static_cast<float>(2 * M_PI * 0.1 * k));
        auto y = exchange(a, b, 0, n, {tone});
        std::complex<double> up = 0, down = 0;
        for (size_t k = 0; k < n; ++k) {
            up += std::complex<double>(y[k]) * std::polar(1.0, -2 * M_PI * 0.1 * k);
            down += std::complex<double>(y[k]) * std::polar(1.0, 2 * M_PI * 0.1 * k);
        }
        const double a_ = std::pow(10.0, 1.0 / 20), phi = 5.0 * M_PI / 180;
        const std::complex<double> mu = (1.0 + a_ * std::polar(1.0, phi)) / 2.0, nu = (1.0 - a_ * std::polar(1.0, -phi)) / 2.0;
        const double want = 10 * std::log10(std::norm(nu) / std::norm(mu)), got = 10 * std::log10(std::norm(down) / std::norm(up));
        CHECK(std::abs(got - want) < 0.1, "IQ image ratio %.2f dB, expected %.2f dB", got, want);
        b.setImpairments(sim::Impairments());
    }

    // 12. DC offset is added to every sample.
    {
        sim::Impairments imp;
        imp.dc = fc32(0.01f, -0.02f);
        b.setImpairments(imp);
        auto y = exchange(a, b, 0, 3000, {});
        double err = 0;
        for (auto v : y)
            err = std::max(err, (double) std::abs(v - imp.dc));
        CHECK(err < 1e-7, "DC offset error %g", err);
        b.setImpairments(sim::Impairments());
    }

    // 13. Gain model: TX and RX gains scale the signal relative to their
    // references, RX gain also scales noise, and the ADC clips at full scale.
    {
        sim::Impairments imp;
        imp.gain_model = true;
        imp.tx_gain_ref_db = 10.0;
        imp.rx_gain_ref_db = 20.0;
        a.setImpairments(imp);
        a.setTXGain(16.0);   // x2 in amplitude
        b.setImpairments(imp);
        b.setRXGain(14.0);   // x1/2: net unity
        auto y = exchange(a, b, 1000, 6000, burst(3000));
        double err = 0;
        for (size_t k = 0; k < 3000; ++k)
            err = std::max(err, (double) std::abs(y[1000 + k] - x[k]));
        CHECK(err < 1e-5, "TX +6 dB and RX -6 dB: error %g", err);

        // RX gain +12 dB over unity: about 4x amplitude (peaks 3.6), full scale 1.
        imp.adc_full_scale = 1.0;
        b.setImpairments(imp);
        b.setRXGain(26.0);
        auto s0 = b.getStats();
        y = exchange(a, b, 1000, 6000, burst(3000));
        auto s1 = b.getStats();
        double peak = 0, err_small = 0;
        unsigned clipped = 0;
        for (size_t k = 0; k < 3000; ++k) {
            const fc32 v = y[1000 + k], w = static_cast<float>(std::pow(10.0, 12.0 / 20)) * x[k];
            peak = std::max({peak, (double) std::abs(v.real()), (double) std::abs(v.imag())});
            if (std::abs(w.real()) < 0.99f && std::abs(w.imag()) < 0.99f)
                err_small = std::max(err_small, (double) std::abs(v - w));
            clipped += (std::abs(w.real()) > 1.0f) + (std::abs(w.imag()) > 1.0f);
        }
        CHECK(peak <= 1.0, "ADC output exceeds full scale: %g", peak);
        CHECK(err_small < 1e-4, "unclipped samples changed: %g", err_small);
        CHECK(clipped > 0 && s1.rx_clipped - s0.rx_clipped == clipped,
              "clip counter %llu, expected %u", (unsigned long long) (s1.rx_clipped - s0.rx_clipped), clipped);

        // Noise is input-referred: +10 dB of RX gain gives +10 dB of noise.
        imp.adc_full_scale = 0.0;
        b.setImpairments(imp);
        b.setRXGain(30.0);
        b.setNoisePower(-20.0);
        y = exchange(a, b, 0, 50000, {});
        double p = 0;
        for (auto v : y)
            p += std::norm(v);
        p /= y.size();
        CHECK(std::abs(10 * std::log10(p) + 10.0) < 0.2, "noise with +10 dB RX gain %g dB, expected -10", 10 * std::log10(p));
        b.setNoisePower(-300.0);
        a.setImpairments(sim::Impairments());
        b.setImpairments(sim::Impairments());
    }

    // 15. TX-to-RX leakage: a device with leakage hears its own transmission
    // at its own sample index, scaled by the leakage gain.
    {
        sim::Impairments imp;
        imp.tx_leakage_db = -20.0;
        a.setImpairments(imp);
        auto y = exchange(a, a, 500, 5000, burst(3000));
        double err = 0;
        for (size_t k = 0; k < 3000; ++k)
            err = std::max(err, (double) std::abs(y[500 + k] - 0.1f * x[k]));
        CHECK(err < 1e-5, "own transmission leakage error %g", err);
        a.setImpairments(sim::Impairments());
    }

    // 9. Synthetic traffic: frames are valid Ethernet/IPv4/UDP, delivery is
    //    hashed, deduplicated and corruption is detected.
    {
        sim::SimTraffic t1(1, "10.10.10.%d", "c6:ff:ff:ff:ff:%02x", 1500);
        sim::SimTraffic t2(2, "10.10.10.%d", "c6:ff:ff:ff:ff:%02x", 1500);
        auto f = t1.buildFrame(2, 5001, 42, 600, 0);
        CHECK(f.size() == 14 + 20 + 8 + 600, "frame size %zu", f.size());
        CHECK(f[5] == 2 && f[11] == 1 && f[0] == 0xc6 && f[12] == 0x08 && f[13] == 0x00, "Ethernet header");
        CHECK(f[14] == 0x45 && f[23] == 17 && f[29] == 1 && f[33] == 2, "IPv4/UDP header");
        unsigned sum = 0;
        for (int i = 14; i < 34; i += 2)
            sum += (f[i] << 8) | f[i + 1];
        while (sum >> 16)
            sum = (sum & 0xffff) + (sum >> 16);
        CHECK(sum == 0xffff, "IPv4 checksum");
        const uint8_t *m = f.data() + 42;   // UDP payload: MGEN header first
        CHECK(((m[0] << 8) | m[1]) == 600 && m[2] == 2 &&
              ((m[4] << 24) | (m[5] << 16) | (m[6] << 8) | m[7]) == 5001 &&
              ((m[8] << 24) | (m[9] << 16) | (m[10] << 8) | m[11]) == 42, "MGEN header (size, version, flow, seq)");
        auto deliver = [&](const std::vector<uint8_t> &frame) {
            std::vector<unsigned char> d(sizeof(ExtendedHeader) + frame.size());
            std::memcpy(d.data() + sizeof(ExtendedHeader), frame.data(), frame.size());
            Header h = {};
            auto pkt = std::make_shared<RadioPacket>(h, d.data(), d.size());
            pkt->ehdr().data_len = frame.size();
            t2.sink.send(std::move(pkt));
        };
        deliver(f);
        deliver(f);
        auto bad = t1.buildFrame(2, 5001, 43, 600, 0);
        bad[100] ^= 1;
        deliver(bad);
        deliver(t1.buildFrame(2, 5001, 7, 600, 0));
        auto r = t2.getReceived()[{1, 5001}];
        CHECK(r.unique == 2 && r.duplicates == 1 && r.corrupt == 1 && r.out_of_order == 1,
              "unique %lu dup %lu corrupt %lu ooo %lu", r.unique, r.duplicates, r.corrupt, r.out_of_order);
    }

    // 14. Independent simulated clock (Clock::setSimulatedClock): device
    // time = host + offset + ppm (host - epoch). Both devices here share the process
    // clock, so a burst must still land where it was sent: exactly with an
    // offset only, within one sample with a rate error (two roundings). The
    // clock only moves forward here (a device's rendering position cannot
    // move back), so this runs last and leaves the clock set.
    for (auto [offset, ppm] : {std::pair<double, double>{0.25, 0.0}, {1.5, 0.0}, {1.6, 40.0}}) {
        const double epoch = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        Clock::setSimulatedClock(offset, ppm);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double host = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double dev = MonoClock::now().get_real_secs();
        const double want = host + offset + 1e-6 * ppm * (host - epoch);
        CHECK(std::abs(dev - want) < 1e-4, "simulated clock %.6f s, expected %.6f s", dev, want);
        auto y = exchange(a, b, 1000, 6000, burst(3000));
        int best = 0;
        double best_err = 1e9;
        for (int shift = -2; shift <= 2; ++shift) {
            double err = 0;
            for (size_t k = 10; k + 10 < 3000; ++k)
                err = std::max(err, (double) std::abs(y[1000 + shift + k] - x[k]));
            if (err < best_err) { best_err = err; best = shift; }
        }
        CHECK(best_err < 1e-4 && (ppm == 0 ? best == 0 : std::abs(best) <= 1),
              "clock offset %g ppm %g: burst at %+d samples (error %g)", offset, ppm, best, best_err);
    }

    a.stop();
    b.stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
