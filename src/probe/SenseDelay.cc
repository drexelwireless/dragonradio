// Host sensing-delay probe. See SenseDelay.hh.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "probe/SenseDelay.hh"

namespace probe {

using fc32 = std::complex<float>;

static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
static constexpr double kPi = 3.14159265358979323846;

static double secs(const MonoClock::time_point &t)
{
    return static_cast<double>(t.time_since_epoch().count());
}

static MonoClock::time_point fromTicks(int64_t ticks, double fs)
{
    const int64_t full = static_cast<int64_t>(std::floor(ticks/fs));

    return MonoClock::time_point{MonoClock::duration{timerep_t{full, (ticks - full*fs)/fs}}};
}

double SenseDelayProbe::hostNow(void)
{
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    return static_cast<double>(ns)*1e-9;
}

SenseDelayProbe::SenseDelayProbe(std::shared_ptr<Radio> usrp, const SenseDelayConfig &config)
  : usrp_(std::move(usrp))
  , config_(config)
  , done_(false)
  , offset_(0.0)
  , offset_valid_(false)
  , calibrated_(false)
{
    if (config_.window == 0)
        config_.window = 1;

    // Tone at fs/8 with 16-sample raised-cosine ramps: clear of DC, where
    // receivers have their offset, and without hard edges.
    const size_t n = std::max<size_t>(config_.burst_len, 1);
    const size_t ramp = std::min<size_t>(16, n/2);

    burst_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        float a = config_.amplitude;

        if (i < ramp)
            a *= 0.5f*(1.0f - std::cos(static_cast<float>(kPi)*i/ramp));
        else if (i >= n - ramp)
            a *= 0.5f*(1.0f - std::cos(static_cast<float>(kPi)*(n - 1 - i)/ramp));
        burst_[i] = std::polar(a, static_cast<float>(2.0*kPi*i/8.0));
    }
}

SenseDelayProbe::~SenseDelayProbe()
{
    done_ = true;
}

std::list<std::shared_ptr<IQBuf>> SenseDelayProbe::makeBurst(void) const
{
    auto b = std::make_shared<IQBuf>(burst_.size());

    std::copy(burst_.begin(), burst_.end(), &(*b)[0]);
    return {b};
}

double SenseDelayProbe::deviceNowEstimate(void) const
{
    return hostNow() + offset_.load(std::memory_order_acquire);
}

void SenseDelayProbe::calibrateOnce(void)
{
    double best_rtt = std::numeric_limits<double>::infinity();
    double best_off = 0.0;
    double best_host = 0.0;

    for (unsigned i = 0; i < std::max(config_.cal_trials, 1u); ++i) {
        double h0 = hostNow();
        double d = secs(usrp_->now());
        double h1 = hostNow();

        if (h1 - h0 < best_rtt) {
            best_rtt = h1 - h0;
            best_host = 0.5*(h0 + h1);
            best_off = d - best_host;
        }
    }

    offset_.store(best_off, std::memory_order_release);
    offset_valid_.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lock(results_mutex_);

    res_.cal_host.push_back(best_host);
    res_.cal_offset.push_back(best_off);
    res_.cal_rtt.push_back(best_rtt);
}

void SenseDelayProbe::calLoop(void)
{
    double next = hostNow() + config_.cal_interval;

    while (!done_) {
        if (hostNow() >= next) {
            calibrateOnce();
            next += config_.cal_interval;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void SenseDelayProbe::rxLoop(void)
{
    const double fs = usrp_->getRXRate();
    const size_t chunk = std::max<size_t>(config_.chunk, 1);

    res_.chunk_samps = chunk;
    res_.rx_rate = fs;

    while (!done_) {
        MonoClock::time_point t = fromTicks(std::llround((secs(usrp_->now()) + 0.05)*fs), fs);

        win_fill_ = 0;
        win_acc_ = 0.0;
        above_ = false;

        usrp_->startRXStream(t);

        while (!done_) {
            IQBuf buf(usrp_->getRecommendedBurstRXSize(chunk));
            bool  ok = usrp_->burstRX(t, chunk, buf);
            double h = hostNow();

            if (!ok) {
                res_.rx_errors++;
                break;
            }

            size_t n = buf.nsamples.load(std::memory_order_acquire);

            if (!buf.timestamp || n == 0)
                continue;

            double t0 = secs(*buf.timestamp);

            res_.chunk_dev_end.push_back(t0 + n/fs);
            res_.chunk_host.push_back(h);
            res_.chunk_n.push_back(static_cast<uint32_t>(n));

            process(buf, n, h);

            t = *buf.timestamp + MonoClock::duration(static_cast<double>(n)/fs);
        }

        usrp_->stopRXStream();
    }
}

void SenseDelayProbe::process(const IQBuf &buf, size_t n, double host)
{
    const double  fs = res_.rx_rate;
    const int64_t tick0 = std::llround(secs(*buf.timestamp)*fs);
    const size_t  w = config_.window;

    for (size_t i = 0; i < n; ++i) {
        const int64_t tick = tick0 + static_cast<int64_t>(i);

        if (win_fill_ == 0)
            win_start_tick_ = tick;
        else if (tick != win_start_tick_ + static_cast<int64_t>(win_fill_)) {
            // Discontinuity (overflow or restart): start a fresh window.
            win_fill_ = 0;
            win_acc_ = 0.0;
            win_start_tick_ = tick;
        }

        win_acc_ += std::norm(buf[i]);

        if (++win_fill_ == w) {
            onWindow(win_start_tick_, win_acc_/w, host);
            win_fill_ = 0;
            win_acc_ = 0.0;
        }
    }
}

void SenseDelayProbe::onWindow(int64_t start_tick, double power, double host)
{
    if (!calibrated_) {
        if (config_.threshold > 0.0) {
            res_.threshold = config_.threshold;
        } else {
            noise_windows_.push_back(power);
            if (host - rx_start_host_ < config_.noise_secs || noise_windows_.size() < 8)
                return;

            auto mid = noise_windows_.begin() + noise_windows_.size()/2;

            std::nth_element(noise_windows_.begin(), mid, noise_windows_.end());
            res_.noise_power = *mid;
            res_.threshold = *mid*std::pow(10.0, config_.threshold_db/10.0);
            noise_windows_.clear();
        }
        calibrated_ = true;
        return;
    }

    const bool above = power > res_.threshold;

    if (above && !above_) {
        const double dev = static_cast<double>(start_tick)/res_.rx_rate;
        const double ref = res_.noise_power > 0.0 ? res_.noise_power : res_.threshold;

        res_.edge_dev.push_back(dev);
        res_.edge_host.push_back(host);
        res_.edge_power_db.push_back(static_cast<float>(10.0*std::log10(power/ref)));

        if (config_.mode == Mode::kReact && host - last_react_host_ >= config_.holdoff) {
            last_react_host_ = host;
            {
                std::lock_guard<std::mutex> lock(tx_mutex_);

                tx_pending_ = true;
                tx_edge_dev_ = dev;
                tx_trigger_host_ = hostNow();
            }
            tx_cond_.notify_one();
        }
    }

    above_ = above;
}

void SenseDelayProbe::txReactLoop(void)
{
    const double fs = usrp_->getTXRate();
    auto         bufs = makeBurst();

    while (!done_) {
        double edge_dev;
        double trigger_host;

        {
            std::unique_lock<std::mutex> lock(tx_mutex_);

            tx_cond_.wait_for(lock, std::chrono::milliseconds(100),
                              [this]{ return done_ || tx_pending_; });
            if (done_)
                return;
            if (!tx_pending_)
                continue;
            tx_pending_ = false;
            edge_dev = tx_edge_dev_;
            trigger_host = tx_trigger_host_;
        }

        std::optional<MonoClock::time_point> when;
        double                               tx_dev = kNaN;

        if (config_.lead > 0.0) {
            int64_t ticks = std::llround((deviceNowEstimate() + config_.lead)*fs);

            when = fromTicks(ticks, fs);
            tx_dev = static_cast<double>(ticks)/fs;
        }

        usrp_->burstTX(when, true, true, bufs);

        double sent = hostNow();

        std::lock_guard<std::mutex> lock(results_mutex_);

        res_.react_edge_dev.push_back(edge_dev);
        res_.react_tx_dev.push_back(tx_dev);
        res_.react_host_trigger.push_back(trigger_host);
        res_.react_host_sent.push_back(sent);
    }
}

void SenseDelayProbe::txPingLoop(void)
{
    const double fs = usrp_->getTXRate();
    auto         bufs = makeBurst();

    while (!done_ && !calibrated_)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    double next = hostNow();

    while (!done_) {
        next += config_.period;
        while (!done_ && hostNow() < next)
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (done_)
            return;

        std::optional<MonoClock::time_point> when;
        double                               tx_dev = kNaN;

        if (config_.lead > 0.0) {
            int64_t ticks = std::llround((deviceNowEstimate() + config_.lead)*fs);

            when = fromTicks(ticks, fs);
            tx_dev = static_cast<double>(ticks)/fs;
        }

        usrp_->burstTX(when, true, true, bufs);

        double sent = hostNow();

        std::lock_guard<std::mutex> lock(results_mutex_);

        res_.ping_tx_dev.push_back(tx_dev);
        res_.ping_host_sent.push_back(sent);
    }
}

SenseDelayResults SenseDelayProbe::run(double duration)
{
    res_ = SenseDelayResults();
    done_ = false;
    calibrated_ = false;
    noise_windows_.clear();
    last_react_host_ = -1e9;
    tx_pending_ = false;

    // Clear device counters left from before the run.
    usrp_->getTXLateCount();
    usrp_->getTXUnderflowCount();

    calibrateOnce();
    rx_start_host_ = hostNow();

    std::thread rx([this]{ rxLoop(); });
    std::thread cal([this]{ calLoop(); });
    std::thread tx;

    if (config_.mode == Mode::kReact)
        tx = std::thread([this]{ txReactLoop(); });
    else if (config_.mode == Mode::kPing)
        tx = std::thread([this]{ txPingLoop(); });

    const double end = hostNow() + duration;

    while (hostNow() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    done_ = true;
    tx_cond_.notify_all();

    if (tx.joinable())
        tx.join();
    cal.join();
    rx.join();

    calibrateOnce();

    // Late-burst reports from the device arrive asynchronously.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    res_.tx_late = usrp_->getTXLateCount();
    res_.tx_underflow = usrp_->getTXUnderflowCount();

    return res_;
}

}
