// Simulated radio device for full-stack simulation (phase P2).

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include "logging.hh"
#include "sim/SimUSRP.hh"

namespace sim {

namespace {

/** Seconds a reader may fall behind before samples are dropped (overflow). */
constexpr double kOverflowSecs = 0.5;

bool readFull(int fd, void *p, size_t n)
{
    char *c = static_cast<char*>(p);

    while (n > 0) {
        ssize_t r = ::read(fd, c, n);

        if (r == 0)
            return false;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        c += r;
        n -= r;
    }

    return true;
}

bool sendFull(int fd, struct iovec *iov, int iovcnt)
{
    while (iovcnt > 0) {
        struct msghdr msg = {};

        msg.msg_iov = iov;
        msg.msg_iovlen = iovcnt;

        ssize_t w = ::sendmsg(fd, &msg, MSG_NOSIGNAL);

        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }

        while (iovcnt > 0 && static_cast<size_t>(w) >= iov->iov_len) {
            w -= iov->iov_len;
            ++iov;
            --iovcnt;
        }

        if (iovcnt > 0) {
            iov->iov_base = static_cast<char*>(iov->iov_base) + w;
            iov->iov_len -= w;
        }
    }

    return true;
}

/** Seconds since the clock's epoch. */
double secs(const MonoClock::time_point &t)
{
    return static_cast<double>(t.time_since_epoch().count());
}

/** Host monotonic time (sec). */
double hostNow(void)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

int64_t SimUSRP::toTicks(const MonoClock::time_point &t, double fs)
{
    // As uhd::time_spec_t::to_ticks: whole seconds times the integer rate are
    // exact, so sample indices of a 1e5 s clock lose no precision.
    const timerep_t r = t.time_since_epoch().count();
    const int64_t   rate_i = static_cast<int64_t>(fs);
    const double    rate_f = fs - rate_i;

    return r.get_full()*rate_i + std::llround(r.get_frac()*rate_i + static_cast<double>(r)*rate_f);
}

int64_t SimUSRP::ceilTicks(const MonoClock::time_point &t, double fs)
{
    int64_t n = toTicks(t, fs);

    if (secs(fromTicks(n, fs)) < secs(t) - 1e-12)
        ++n;

    return n;
}

MonoClock::time_point SimUSRP::fromTicks(int64_t n, double fs)
{
    // As uhd::time_spec_t::from_ticks
    const int64_t rate_i = static_cast<int64_t>(fs);
    const double  rate_f = fs - rate_i;
    const int64_t full = n/rate_i;
    const double  frac = (static_cast<double>(n - full*rate_i) - full*rate_f)/fs;

    return MonoClock::time_point{MonoClock::duration{timerep_t{full, frac}}};
}

SimUSRP::SimUSRP(const std::string& air_path,
                 uint8_t node_id,
                 double freq,
                 double clock_rate,
                 double rx_latency,
                 uint64_t seed)
  : node_id_(node_id)
  , clock_rate_(clock_rate)
  , rx_latency_(rx_latency)
  , tx_freq_(freq)
  , rx_freq_(freq)
  , tx_rate_(1e6)
  , rx_rate_(1e6)
  , tx_gain_(0.0)
  , rx_gain_(0.0)
  , tx_max_samps_(512)
  , rx_max_samps_(2048)
  , clk_offset_(0.0)
  , clk_rate_(1.0)
  , clk_epoch_(0.0)
  , fd_(-1)
  , done_(false)
  , in_tx_burst_(false)
  , tx_underflow_count_(0)
  , tx_late_count_(0)
  , tx_pos_(0)
  , tx_burst_late_(false)
  , tx_burst_count_(0)
  , tx_group_(0)
  , queued_samples_(0)
  , rx_pos_(0)
  , rendered_upto_(0)
  , streaming_(false)
  , noise_db_(-200.0)
  , noise_std_(0.0f)
  , rng_(seed)
  , noise_rng_(seed ^ 0x9e3779b97f4a7c15ull)
{
    tx_lead_time_.store(std::chrono::duration<double>(0.0));
    tx_rampup_.store(std::chrono::duration<double>(0.0));

    struct sockaddr_un addr = {};

    if (air_path.size() >= sizeof(addr.sun_path))
        throw std::invalid_argument("air socket path too long: " + air_path);

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, air_path.c_str(), sizeof(addr.sun_path)-1);

    // The relay may still be starting; retry for up to 10 seconds. There is
    // no fallback: without the relay there is no device.
    for (int attempt = 0; ; ++attempt) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0)
            throw std::runtime_error(std::string("socket: ") + strerror(errno));

        if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0)
            break;

        int err = errno;

        ::close(fd_);
        fd_ = -1;

        if (attempt >= 100)
            throw std::runtime_error("cannot connect to air relay " + air_path + ": " + strerror(err));

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    AirHeader hello = {};

    hello.magic = kAirMagic;
    hello.version = kAirVersion;
    hello.type = kAirHello;
    hello.node = node_id_;

    struct iovec iov = { &hello, sizeof(hello) };

    if (!sendFull(fd_, &iov, 1)) {
        int err = errno;

        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error(std::string("air relay hello: ") + strerror(err));
    }

    reader_ = std::thread(&SimUSRP::reader, this);

    pthread_setname_np(reader_.native_handle(), "dr-sim-reader");

    logUSRP(LOGINFO, "Simulated device node %u connected to %s", node_id_, air_path.c_str());
}

SimUSRP::~SimUSRP()
{
    stop();

    if (fd_ >= 0)
        ::close(fd_);
}

void SimUSRP::setClock(double offset, double ppm)
{
    clk_epoch_.store(hostNow(), std::memory_order_relaxed);
    clk_offset_.store(offset, std::memory_order_relaxed);
    clk_rate_.store(1.0 + 1e-6*ppm, std::memory_order_release);
}

double SimUSRP::deviceToHost(double device) const
{
    const double rate = clk_rate_.load(std::memory_order_acquire);
    const double epoch = clk_epoch_.load(std::memory_order_relaxed);

    return epoch + (device - clk_offset_.load(std::memory_order_relaxed) - epoch)/rate;
}

double SimUSRP::hostToDevice(double host) const
{
    const double rate = clk_rate_.load(std::memory_order_acquire);

    return host + clk_offset_.load(std::memory_order_relaxed) + (rate - 1.0)*(host - clk_epoch_.load(std::memory_order_relaxed));
}

MonoClock::time_point SimUSRP::now() noexcept
{
    const auto   host = std::chrono::steady_clock::now().time_since_epoch();
    const double rate = clk_rate_.load(std::memory_order_acquire);
    const double off = clk_offset_.load(std::memory_order_relaxed);

    // Exactly the host clock (as Radio::now) unless setClock was used.
    if (off == 0.0 && rate == 1.0)
        return MonoClock::time_point{host};

    const double h = std::chrono::duration<double>(host).count();

    return MonoClock::time_point{host} + MonoClock::duration{timerep_t{off + (rate - 1.0)*(h - clk_epoch_.load(std::memory_order_relaxed))}};
}

void SimUSRP::setLink(uint8_t src, const Link &link)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    links_[src] = link;
}

Link SimUSRP::getLink(uint8_t src)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);
    auto                        it = links_.find(src);

    return it == links_.end() ? Link() : it->second;
}

void SimUSRP::setImpairments(const Impairments &imp)
{
    std::scoped_lock lock(tx_mutex_, rx_mutex_);

    impairments_ = imp;
}

Impairments SimUSRP::getImpairments(void)
{
    std::scoped_lock lock(tx_mutex_, rx_mutex_);

    return impairments_;
}

double SimUSRP::getNoisePower(void)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    return noise_db_;
}

void SimUSRP::setNoisePower(double db)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    noise_db_ = db;
    noise_std_ = std::sqrt(std::pow(10.0, db/10.0)/2.0);
}

Stats SimUSRP::getStats(void)
{
    std::scoped_lock lock(tx_mutex_, rx_mutex_);

    return stats_;
}

namespace {

// Windowed-sinc fractional interpolator: 32 taps, Blackman window, 1024
// phases (phase quantization at most 1/2048 sample).
struct Interpolator {
    static constexpr int kTaps = 32;
    static constexpr int kPhases = 1024;
    std::vector<float> table;

    Interpolator() : table(static_cast<size_t>(kPhases + 1)*kTaps)
    {
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = static_cast<double>(p)/kPhases;
            double       sum = 0;

            for (int k = 0; k < kTaps; ++k) {
                // Tap k multiplies x[floor(t) - kTaps/2 + 1 + k].
                const double tau = frac - (k - kTaps/2 + 1);
                const double sinc = tau == 0 ? 1.0 : std::sin(M_PI*tau)/(M_PI*tau);
                const double u = (tau + kTaps/2)/kTaps;   // window over [-kTaps/2, kTaps/2]
                const double w = (u <= 0 || u >= 1) ? 0.0
                    : 0.42 - 0.5*std::cos(2*M_PI*u) + 0.08*std::cos(4*M_PI*u);

                table[p*kTaps + k] = static_cast<float>(sinc*w);
                sum += sinc*w;
            }
            for (int k = 0; k < kTaps; ++k)
                table[p*kTaps + k] = static_cast<float>(table[p*kTaps + k]/sum);
        }
    }
};

// y[k] = x(k * ratio), k = 0 .. floor((N - 1)/ratio); zero outside x.
std::vector<std::complex<float>> resample(const std::vector<std::complex<float>> &x, double ratio)
{
    static const Interpolator interp;
    constexpr int             T = Interpolator::kTaps;
    const size_t              n = x.empty() ? 0 : static_cast<size_t>(std::floor((x.size() - 1)/ratio)) + 1;
    std::vector<std::complex<float>> y(n);
    const int64_t             len = static_cast<int64_t>(x.size());

    for (size_t k = 0; k < n; ++k) {
        const double  t = k*ratio;
        const int64_t i = static_cast<int64_t>(std::floor(t));
        const int     p = static_cast<int>(std::lround((t - i)*Interpolator::kPhases));
        const float  *h = &interp.table[static_cast<size_t>(p)*T];
        std::complex<float> acc(0.0f, 0.0f);

        for (int j = 0; j < T; ++j) {
            const int64_t m = i - T/2 + 1 + j;

            if (m >= 0 && m < len)
                acc += h[j]*x[m];
        }
        y[k] = acc;
    }
    return y;
}

}

void SimUSRP::sendBurst(int64_t start, const std::complex<float> *x, size_t n)
{
    const double fs = tx_rate_.load(std::memory_order_relaxed);
    const double fc = tx_freq_.load(std::memory_order_relaxed);
    const double ppm = getClockPPM();
    AirHeader    hdr = {};

    hdr.magic = kAirMagic;
    hdr.version = kAirVersion;
    hdr.type = kAirBurst;
    hdr.node = node_id_;
    // On the air, sample indices are on the shared host clock; this device's
    // clock may have an offset and rate error (setClock).
    if (getClockOffset() == 0.0 && ppm == 0.0)
        hdr.start = start;
    else
        hdr.start = std::llround(deviceToHost(static_cast<double>(start)/fs)*fs);
    hdr.fs = fs;
    hdr.fc = fc;
    hdr.nsamples = n;
    hdr.clock_ppm = ppm;
    hdr.burst = tx_burst_count_++;
    hdr.group = tx_group_;

    const double tx_gain = tx_gain_.load(std::memory_order_relaxed);

    if (impairments_.gain_model && tx_gain != impairments_.tx_gain_ref_db) {
        const float g = static_cast<float>(std::pow(10.0, (tx_gain - impairments_.tx_gain_ref_db)/20.0));

        tx_scaled_.resize(n);
        for (size_t i = 0; i < n; ++i)
            tx_scaled_[i] = g*x[i];
        x = tx_scaled_.data();
    }

    struct iovec iov[2] = { { &hdr, sizeof(hdr) },
                            { const_cast<std::complex<float>*>(x), n*sizeof(std::complex<float>) } };

    if (!sendFull(fd_, iov, 2)) {
        if (!done_)
            logUSRP(LOGERROR, "Simulated device: air relay write failed: %s", strerror(errno));
        return;
    }

    stats_.tx_bursts++;
    stats_.tx_samples += n;
    for (size_t i = 0; i < n; ++i)
        stats_.tx_nonfinite += !std::isfinite(x[i].real()) || !std::isfinite(x[i].imag());

    // TX-to-RX leakage: the burst also reaches this device's own receiver,
    // at its own sample index (same clock, no carrier offset).
    if (impairments_.tx_leakage_db) {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        Burst                       b;

        b.src = node_id_;
        b.start = start;
        b.fs = fs;
        b.shift_hz = fc - rx_freq_.load(std::memory_order_relaxed);
        b.gain = static_cast<float>(std::pow(10.0, *impairments_.tx_leakage_db/20.0));
        b.x.assign(x, x + n);
        if (b.end() > rendered_upto_ && queued_samples_ + n <= kMaxQueuedSamples) {
            queued_samples_ += n;
            bursts_.push_back(std::move(b));
        }
    }
}

std::optional<MonoClock::time_point> SimUSRP::getNextTXTime() const
{
    std::lock_guard<std::mutex> lock(tx_mutex_);

    if (!in_tx_burst_.load(std::memory_order_acquire))
        return std::nullopt;

    if (t_next_tx_ && *t_next_tx_ < const_cast<SimUSRP*>(this)->now())
        return std::nullopt;

    return t_next_tx_;
}

void SimUSRP::advanceTX(ssize_t n)
{
    tx_pos_ += n;

    if (t_next_tx_)
        *t_next_tx_ += MonoClock::duration(static_cast<double>(n)/tx_rate_.load(std::memory_order_relaxed));
}

void SimUSRP::zeroStuff(ssize_t n)
{
    std::lock_guard<std::mutex> lock(tx_mutex_);

    // Zeros add nothing to the air: only the burst's position advances.
    advanceTX(n);
}

void SimUSRP::burstTX(std::optional<MonoClock::time_point> when,
                      bool start_of_burst,
                      bool end_of_burst,
                      std::list<std::shared_ptr<IQBuf>>& bufs)
{
    std::lock_guard<std::mutex> lock(tx_mutex_);
    const MonoClock::time_point t_now = now();
    const double                fs = tx_rate_.load(std::memory_order_relaxed);
    const MonoClock::duration   latency(rx_latency_);

    if (start_of_burst) {
        std::chrono::duration<double> tx_rampup = tx_rampup_.load(std::memory_order_acquire);

        tx_burst_late_ = false;
        ++tx_group_;

        if (when) {
            // As USRP::burstTX, the burst starts tx_rampup early with zeros.
            *when -= MonoClock::duration(tx_rampup.count());

            t_next_tx_ = *when;
            tx_pos_ = toTicks(*when, fs);

            // UHD drops a timed burst whose time has passed (EVENT_CODE_TIME_ERROR)
            if (*when < t_now) {
                tx_burst_late_ = true;
                tx_late_count_.fetch_add(1, std::memory_order_relaxed);
                stats_.tx_late++;
                logUSRP(LOGWARNING, "TX error: packet had time that was late");
            }
        } else {
            t_next_tx_ = std::nullopt;
            tx_pos_ = toTicks(t_now + latency, fs);
        }

        in_tx_burst_.store(true, std::memory_order_release);

        if (tx_rampup.count() != 0)
            advanceTX(static_cast<ssize_t>(tx_rampup.count()*fs));
    } else if (!tx_burst_late_ && fromTicks(tx_pos_, fs) < t_now) {
        // A continuation that arrives after its air time underflows: the
        // device runs dry and the samples go out as soon as they arrive.
        tx_underflow_count_.fetch_add(1, std::memory_order_relaxed);
        stats_.tx_underflow++;
        tx_pos_ = toTicks(t_now + latency, fs);
        if (t_next_tx_)
            t_next_tx_ = fromTicks(tx_pos_, fs);
        logUSRP(LOGWARNING, "TX error: an internal send buffer has emptied");
    }

    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        IQBuf &iqbuf = **it;

        if (t_next_tx_)
            iqbuf.timestamp = *t_next_tx_ - MonoClock::duration(iqbuf.delay/fs);

        size_t n = iqbuf.size() > iqbuf.delay ? iqbuf.size() - iqbuf.delay : 0;

        if (!tx_burst_late_ && n > 0)
            sendBurst(tx_pos_, &iqbuf[iqbuf.delay], n);

        advanceTX(n);
    }

    if (end_of_burst) {
        tx_burst_late_ = false;
        in_tx_burst_.store(false, std::memory_order_release);
    }
}

void SimUSRP::stopTXBurst(void)
{
    std::lock_guard<std::mutex> lock(tx_mutex_);

    tx_burst_late_ = false;
    t_next_tx_ = std::nullopt;
    in_tx_burst_.store(false, std::memory_order_release);
}

void SimUSRP::startRXStream(std::optional<MonoClock::time_point> when)
{
    const MonoClock::time_point t = when ? *when : now();
    std::lock_guard<std::mutex> lock(rx_mutex_);

    streaming_ = true;
    rx_pos_ = std::max(ceilTicks(t, rx_rate_.load(std::memory_order_relaxed)), rendered_upto_);
    stats_.rx_streams++;
}

void SimUSRP::stopRXStream(void)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    streaming_ = false;
}

void SimUSRP::waitUntil(const MonoClock::time_point &t)
{
    while (!done_) {
        double d = secs(t) - secs(now());

        if (d <= 0)
            return;

        std::this_thread::sleep_for(std::chrono::duration<double>(std::min(d, 0.05)));
    }
}

void SimUSRP::render(int64_t n0, size_t n, std::complex<float> *out)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);
    const int64_t               n1 = n0 + static_cast<int64_t>(n);

    if (out) {
        if (noise_std_ > 0) {
            for (size_t i = 0; i < n; ++i)
                out[i] = noise_rng_(noise_std_);
        } else
            std::fill(out, out + n, std::complex<float>(0.0f, 0.0f));

        for (const Burst &b : bursts_) {
            int64_t lo = std::max(n0, b.start);
            int64_t hi = std::min(n1, b.end());

            if (lo >= hi)
                continue;

            const std::complex<float> *x = b.x.data() + (lo - b.start);
            std::complex<float>       *y = out + (lo - n0);
            size_t                     len = hi - lo;

            if (b.shift_hz == 0.0) {
                for (size_t i = 0; i < len; ++i)
                    y[i] += b.gain*x[i];
            } else {
                // Phase is a function of absolute sample time, so a carrier
                // offset stays continuous across chunks and bursts.
                double cycles = b.shift_hz*static_cast<double>(lo)/b.fs;
                double frac = cycles - std::floor(cycles);
                std::complex<double> osc = std::polar(static_cast<double>(b.gain), 2*M_PI*frac);
                std::complex<double> rot = std::polar(1.0, 2*M_PI*b.shift_hz/b.fs);

                for (size_t i = 0; i < len; ++i) {
                    y[i] += x[i]*std::complex<float>(osc);
                    osc *= rot;
                }
            }
        }

        if (impairments_.receiverActive())
            applyReceiverImpairments(out, n);
    }

    // Retire bursts that end within this chunk.
    for (auto it = bursts_.begin(); it != bursts_.end(); ) {
        if (it->end() <= n1) {
            queued_samples_ -= it->x.size();
            it = bursts_.erase(it);
        } else
            ++it;
    }

    rendered_upto_ = std::max(rendered_upto_, n1);
}

void SimUSRP::applyReceiverImpairments(std::complex<float> *y, size_t n)
{
    const Impairments &imp = impairments_;
    const double rx_gain = rx_gain_.load(std::memory_order_relaxed);
    const float g = imp.gain_model ? static_cast<float>(std::pow(10.0, (rx_gain - imp.rx_gain_ref_db)/20.0)) : 1.0f;
    // IQ imbalance: I' = I, Q' = a (sin(phi) I + cos(phi) Q).
    const float a = static_cast<float>(std::pow(10.0, imp.iq_amp_db/20.0));
    const float phi = static_cast<float>(imp.iq_phase_deg*M_PI/180.0);
    const float qi = a*std::sin(phi), qq = a*std::cos(phi);
    const bool  iq = imp.iq_amp_db != 0 || imp.iq_phase_deg != 0;
    const float fs = static_cast<float>(imp.adc_full_scale);
    uint64_t    clipped = 0;

    for (size_t k = 0; k < n; ++k) {
        float i = g*y[k].real(), q = g*y[k].imag();

        if (iq)
            q = qi*i + qq*q;
        i += imp.dc.real();
        q += imp.dc.imag();
        if (fs > 0) {
            if (std::abs(i) > fs) { i = std::copysign(fs, i); ++clipped; }
            if (std::abs(q) > fs) { q = std::copysign(fs, q); ++clipped; }
        }
        y[k] = std::complex<float>(i, q);
    }
    stats_.rx_clipped += clipped;
}

bool SimUSRP::burstRX(std::optional<MonoClock::time_point> t_start, size_t nsamps, IQBuf& buf)
{
    const double fs = rx_rate_.load(std::memory_order_relaxed);
    int64_t      n_start;
    size_t       ndelivered = 0;

    buf.fc = rx_freq_.load(std::memory_order_relaxed);
    buf.fs = fs;

    {
        std::lock_guard<std::mutex> lock(rx_mutex_);

        if (!streaming_ || done_) {
            buf.complete.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return false;
        }

        // Without a start time, as USRP::burstRX, read the stream from where
        // it is now.
        n_start = t_start ? ceilTicks(*t_start, fs) : rx_pos_;
    }

    const int64_t n_end = n_start + static_cast<int64_t>(nsamps);

    for (;;) {
        const size_t c = std::max<size_t>(getMaxRXSamps(), 1);
        int64_t      pos;

        {
            std::lock_guard<std::mutex> lock(rx_mutex_);

            pos = rx_pos_;
        }

        // Model a host that fell too far behind: the device overflows and the
        // stream resumes at a later time, as UHD reports with 'O'.
        double behind = secs(now()) - secs(fromTicks(pos + c, fs));

        if (behind > kOverflowSecs) {
            int64_t skip = static_cast<int64_t>((behind - rx_latency_)*fs);

            skip -= skip % static_cast<int64_t>(c);
            {
                std::lock_guard<std::mutex> lock(rx_mutex_);

                rx_pos_ += skip;
                pos = rx_pos_;
                stats_.rx_overflows++;
                stats_.rx_overflow_samples += skip;
            }
            render(pos, 0, nullptr);
            logUSRP(LOGWARNING, "RX error: overflow; skipped %ld samples", (long) skip);
        }

        // Release samples only once their time (plus latency) has passed.
        waitUntil(fromTicks(pos + c, fs) + MonoClock::duration(rx_latency_));

        if (done_) {
            buf.complete.store(true, std::memory_order_release);
            return false;
        }

        if (pos < n_start) {
            // UHD discards packets that start before the requested time.
            render(pos, c, nullptr);
            std::lock_guard<std::mutex> lock(rx_mutex_);

            rx_pos_ = pos + c;
            continue;
        }

        render(pos, c, &buf[ndelivered]);

        uint64_t nonfinite = 0;

        for (size_t i = ndelivered; i < ndelivered + c; ++i)
            nonfinite += !std::isfinite(buf[i].real()) || !std::isfinite(buf[i].imag());

        {
            std::lock_guard<std::mutex> lock(rx_mutex_);

            rx_pos_ = pos + c;
            stats_.rx_samples += c;
            stats_.rx_nonfinite += nonfinite;
        }

        if (ndelivered == 0) {
            buf.timestamp = fromTicks(pos, fs);
            if (t_start)
                buf.undersample = pos - n_start;
        }

        ndelivered += c;

        if (t_start ? pos + static_cast<int64_t>(c) >= n_end : ndelivered >= nsamps) {
            buf.resize(ndelivered);
            buf.oversample = (ndelivered - nsamps) + buf.undersample;
            buf.nsamples.store(ndelivered, std::memory_order_release);
            buf.complete.store(true, std::memory_order_release);
            return true;
        } else {
            buf.nsamples.store(ndelivered, std::memory_order_release);

            if (buf.size() < ndelivered + c) {
                logUSRP(LOGERROR,
                    "WARNING: buffer too small to read entire slot: bufsize=%lu, ndelivered=%lu; rx_max_samps=%lu",
                    buf.size(),
                    ndelivered,
                    c);
                buf.complete.store(true, std::memory_order_release);
                return true;
            }
        }
    }
}

void SimUSRP::reader(void)
{
    std::vector<std::complex<float>> x;

    while (!done_) {
        AirHeader hdr;

        if (!readFull(fd_, &hdr, sizeof(hdr)))
            break;

        if (hdr.magic != kAirMagic || hdr.version != kAirVersion) {
            logUSRP(LOGERROR, "Simulated device: bad air header");
            break;
        }

        x.resize(hdr.nsamples);

        if (hdr.nsamples > 0 && !readFull(fd_, x.data(), hdr.nsamples*sizeof(std::complex<float>)))
            break;

        if (hdr.type != kAirBurst || hdr.node == node_id_)
            continue;

        const double                rx_rate = rx_rate_.load(std::memory_order_relaxed);
        const double                rx_freq = rx_freq_.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(rx_mutex_);
        Link                        link;

        stats_.rx_bursts++;

        if (auto it = links_.find(hdr.node); it != links_.end())
            link = it->second;

        if (hdr.fs != rx_rate) {
            stats_.rx_rate_mismatch++;
            continue;
        }

        if (link.erasure > 0 && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < link.erasure) {
            stats_.rx_erased++;
            continue;
        }

        // Carrier offset: tuning difference, both oscillators' errors (as LO
        // frequency errors) and the link's own CFO. Written as
        // (fc - rx)(1 + tx_ppm) + rx (tx_ppm - rx_ppm), equal to
        // fc (1 + tx_ppm) - rx (1 + rx_ppm), so that equal tunings and
        // clocks give exactly zero: the phase of a shift is taken from
        // absolute sample time (about 1e6 s of host uptime), where even a
        // 4e-8 Hz rounding residue (FMA under -Ofast) rotates bursts by
        // 0.26 rad.
        const double rx_ppm = getClockPPM();
        const double tx_ppm = hdr.clock_ppm; // AirHeader is packed
        double shift = (hdr.fc - rx_freq)*(1.0 + 1e-6*tx_ppm) + rx_freq*1e-6*(tx_ppm - rx_ppm) + link.cfo_hz;

        // A transmission tuned outside the receiver's sampled band is not heard.
        if (std::abs(hdr.fc - rx_freq) >= rx_rate/2)
            continue;

        Burst b;

        b.src = hdr.node;
        // Map the true-time start to this device's sample clock.
        if (getClockOffset() == 0.0 && rx_ppm == 0.0)
            b.start = hdr.start + link.delay;
        else
            b.start = std::llround(hostToDevice(static_cast<double>(hdr.start)/hdr.fs)*rx_rate) + link.delay;
        if (link.jitter > 0) {
            const uint32_t group = hdr.group; // AirHeader is packed
            auto           it = jitter_.find(hdr.node);

            if (it == jitter_.end() || it->second.first != group)
                it = jitter_.insert_or_assign(hdr.node, std::make_pair(group,
                    std::uniform_int_distribution<int64_t>(0, link.jitter)(rng_))).first;
            b.start += it->second.second;
        }
        b.fs = hdr.fs;
        b.shift_hz = shift;
        b.gain = static_cast<float>(std::pow(10.0, link.gain_db/20.0));
        // Sample clock offset: the transmitter's clock relative to ours, from
        // both nodes' oscillators and the link's own sfo_ppm.
        const double ratio = (1.0 + 1e-6*tx_ppm)/(1.0 + 1e-6*rx_ppm)*(1.0 + 1e-6*link.sfo_ppm);

        if (ratio != 1.0)
            b.x = resample(x, ratio);
        else
            b.x.assign(x.begin(), x.end());

        if (b.end() <= rendered_upto_) {
            stats_.rx_late++;
            continue;
        }

        if (b.start < rendered_upto_)
            stats_.rx_partial_late++;

        if (queued_samples_ + b.x.size() > kMaxQueuedSamples) {
            stats_.rx_queue_drops++;
            continue;
        }

        queued_samples_ += b.x.size();
        bursts_.push_back(std::move(b));
    }

    if (!done_)
        logUSRP(LOGERROR, "Simulated device: air relay connection closed");
}

void SimUSRP::stop(void)
{
    if (done_.exchange(true))
        return;

    if (fd_ >= 0)
        ::shutdown(fd_, SHUT_RDWR);

    if (reader_.joinable())
        reader_.join();
}

}
