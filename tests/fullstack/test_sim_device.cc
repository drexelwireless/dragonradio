// Tests of the simulated device and synthetic traffic used by full-stack
// simulation (P2). A minimal relay inside the test forwards bursts between two
// SimUSRPs exactly as scripts/sim-air.py does, so sample placement, link
// impairments and UHD-like timing behavior can be checked sample by sample.
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <list>
#include <thread>
#include <vector>

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

    // 8. Synthetic traffic: frames are valid Ethernet/IPv4/UDP, delivery is
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

    a.stop();
    b.stop();
    std::printf(failures ? "FAILED (%d failures)\n" : "PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
