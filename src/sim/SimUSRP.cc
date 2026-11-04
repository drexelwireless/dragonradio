// Simulated radio device for full-stack simulation (phase P2).

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <stdexcept>

#include "logging.hh"
#include "sim/SimUSRP.hh"
#include "util/threads.hh"

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

/** First sample index at or after time t. */
int64_t ceilTicks(const uhd::time_spec_t &t, double fs)
{
    int64_t n = t.to_ticks(fs);

    if (uhd::time_spec_t::from_ticks(n, fs) < t - 1e-12)
        ++n;

    return n;
}

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
  , fd_(-1)
  , done_(false)
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
    struct sockaddr_un addr = {};

    if (air_path.size() >= sizeof(addr.sun_path))
        throw std::invalid_argument("air socket path too long: " + air_path);

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, air_path.c_str(), sizeof(addr.sun_path)-1);

    // The relay may still be starting; retry for up to 10 seconds.
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

    if (!sendFull(fd_, &iov, 1))
        throw std::runtime_error(std::string("air relay hello: ") + strerror(errno));

    // Same defaults as an N210; Radio applies its max-samps factors next.
    setMaxTXSamps(512);
    setMaxRXSamps(2048);

    reader_ = std::thread(&SimUSRP::reader, this);

    setThreadName(reader_, "dr-sim-reader");

    logUSRP(LOGINFO, "Simulated device node %u connected to %s", node_id_, air_path.c_str());
}

SimUSRP::~SimUSRP()
{
    stop();

    if (fd_ >= 0)
        ::close(fd_);
}

void SimUSRP::setClockSource(const std::string clock_source, const size_t mboard)
{
    logUSRP(LOGDEBUG, "Simulated device ignores clock source %s", clock_source.c_str());
}

void SimUSRP::setTimeSource(const std::string &time_source, const size_t mboard)
{
    logUSRP(LOGDEBUG, "Simulated device ignores time source %s", time_source.c_str());
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

void SimUSRP::sendBurst(int64_t start, const std::complex<float> *x, size_t n)
{
    AirHeader hdr = {};

    hdr.magic = kAirMagic;
    hdr.version = kAirVersion;
    hdr.type = kAirBurst;
    hdr.node = node_id_;
    hdr.start = start;
    hdr.fs = tx_rate_;
    hdr.fc = tx_freq_;
    hdr.nsamples = n;
    hdr.burst = tx_burst_count_++;
    hdr.group = tx_group_;

    struct iovec iov[2] = { { &hdr, sizeof(hdr) },
                            { const_cast<std::complex<float>*>(x), n*sizeof(std::complex<float>) } };

    if (!sendFull(fd_, iov, 2)) {
        if (!done_)
            logUSRP(LOGERROR, "Simulated device: air relay write failed: %s", strerror(errno));
        return;
    }

    stats_.tx_bursts++;
    stats_.tx_samples += n;
}

void SimUSRP::burstTX(std::optional<MonoClock::time_point> when,
                      bool start_of_burst,
                      bool end_of_burst,
                      std::list<std::shared_ptr<IQBuf>>& bufs)
{
    std::lock_guard<std::mutex> lock(tx_mutex_);
    MonoClock::time_point       now = MonoClock::now();

    if (start_of_burst) {
        tx_burst_late_ = false;
        ++tx_group_;

        if (when) {
            t_next_tx_ = *when;
            tx_pos_ = when->t.to_ticks(tx_rate_);

            // UHD drops a timed burst whose time has passed (EVENT_CODE_TIME_ERROR)
            if (*when < now) {
                tx_burst_late_ = true;
                tx_late_count_.fetch_add(1, std::memory_order_relaxed);
                stats_.tx_late++;
                logUSRP(LOGWARNING, "TX error: packet had time that was late");
            }
        } else
            tx_pos_ = (now + rx_latency_).t.to_ticks(tx_rate_);
    } else if (!tx_burst_late_ && uhd::time_spec_t::from_ticks(tx_pos_, tx_rate_) < now.t) {
        // A continuation that arrives after its air time underflows: the
        // device runs dry and the samples go out as soon as they arrive.
        tx_underflow_count_.fetch_add(1, std::memory_order_relaxed);
        stats_.tx_underflow++;
        tx_pos_ = (now + rx_latency_).t.to_ticks(tx_rate_);
        logUSRP(LOGWARNING, "TX error: an internal send buffer has emptied");
    }

    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        IQBuf &iqbuf = **it;

        if (t_next_tx_)
            iqbuf.timestamp = *t_next_tx_ - iqbuf.delay/tx_rate_;

        size_t n = iqbuf.size() > iqbuf.delay ? iqbuf.size() - iqbuf.delay : 0;

        if (!tx_burst_late_ && n > 0)
            sendBurst(tx_pos_, &iqbuf[iqbuf.delay], n);

        tx_pos_ += n;

        if (t_next_tx_)
            *t_next_tx_ += static_cast<double>(n)/tx_rate_;
    }

    if (end_of_burst)
        tx_burst_late_ = false;
}

void SimUSRP::stopTXBurst(void)
{
    std::lock_guard<std::mutex> lock(tx_mutex_);

    tx_burst_late_ = false;
    t_next_tx_ = std::nullopt;
}

void SimUSRP::startRXStream(MonoClock::time_point when)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    streaming_ = true;
    rx_pos_ = std::max(ceilTicks(when.t, rx_rate_), rendered_upto_);
    stats_.rx_streams++;
}

void SimUSRP::stopRXStream(void)
{
    std::lock_guard<std::mutex> lock(rx_mutex_);

    streaming_ = false;
}

void SimUSRP::waitUntil(const uhd::time_spec_t &t)
{
    while (!done_) {
        double d = (t - MonoClock::now().t).get_real_secs();

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
            std::normal_distribution<float> dist(0.0f, noise_std_);

            for (size_t i = 0; i < n; ++i)
                out[i] = std::complex<float>(dist(noise_rng_), dist(noise_rng_));
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

bool SimUSRP::burstRX(MonoClock::time_point t_start, size_t nsamps, IQBuf& buf)
{
    int64_t n_start;
    int64_t n_end;
    size_t  ndelivered = 0;

    buf.fc = rx_freq_;
    buf.fs = rx_rate_;

    {
        std::lock_guard<std::mutex> lock(rx_mutex_);

        if (!streaming_ || done_) {
            buf.complete.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return false;
        }
    }

    n_start = ceilTicks(t_start.t, rx_rate_);
    n_end = n_start + static_cast<int64_t>(nsamps);

    for (;;) {
        const size_t c = std::max<size_t>(rx_max_samps_, 1);
        int64_t      pos;

        {
            std::lock_guard<std::mutex> lock(rx_mutex_);

            pos = rx_pos_;
        }

        // Model a host that fell too far behind: the device overflows and the
        // stream resumes at a later time, as UHD reports with 'O'.
        double behind = (MonoClock::now().t - uhd::time_spec_t::from_ticks(pos + c, rx_rate_)).get_real_secs();

        if (behind > kOverflowSecs) {
            int64_t skip = static_cast<int64_t>((behind - rx_latency_)*rx_rate_);

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
        waitUntil(uhd::time_spec_t::from_ticks(pos + c, rx_rate_) + rx_latency_);

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

        {
            std::lock_guard<std::mutex> lock(rx_mutex_);

            rx_pos_ = pos + c;
            stats_.rx_samples += c;
        }

        if (ndelivered == 0) {
            buf.timestamp = MonoClock::time_point { uhd::time_spec_t::from_ticks(pos, rx_rate_) };
            buf.undersample = pos - n_start;
        }

        ndelivered += c;

        if (pos + static_cast<int64_t>(c) >= n_end) {
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

        std::lock_guard<std::mutex> lock(rx_mutex_);
        Link                        link;

        stats_.rx_bursts++;

        if (auto it = links_.find(hdr.node); it != links_.end())
            link = it->second;

        if (hdr.fs != rx_rate_) {
            stats_.rx_rate_mismatch++;
            continue;
        }

        if (link.erasure > 0 && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < link.erasure) {
            stats_.rx_erased++;
            continue;
        }

        double shift = (hdr.fc - rx_freq_) + link.cfo_hz;

        // A transmission tuned outside the receiver's sampled band is not heard.
        if (std::abs(hdr.fc - rx_freq_) >= rx_rate_/2)
            continue;

        Burst b;

        b.src = hdr.node;
        b.start = hdr.start + link.delay;
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
    if (done_)
        return;

    done_ = true;

    if (fd_ >= 0)
        ::shutdown(fd_, SHUT_RDWR);

    if (reader_.joinable())
        reader_.join();
}

}
