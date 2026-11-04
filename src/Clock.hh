// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CLOCK_H_
#define CLOCK_H_

#include <chrono>
#include <memory>

#include <uhd/usrp/multi_usrp.hpp>

template <class T>
struct time_point_t {
    uhd::time_spec_t t;

    explicit time_point_t(double secs=0) : t(secs) {};
    explicit time_point_t(int64_t full_secs, double frac_secs=0) : t(full_secs, frac_secs) {};
    explicit time_point_t(const uhd::time_spec_t &t0) : t(t0) {};

    time_point_t(const time_point_t&) = default;
    time_point_t(time_point_t&&) = default;

    time_point_t& operator=(const time_point_t&) = default;
    time_point_t& operator=(time_point_t&&) = default;

    time_point_t operator +(const time_point_t &other) const
    {
        return time_point_t { t + other.t };
    }

    time_point_t operator +(double delta) const
    {
        return time_point_t { t + delta };
    }

    time_point_t operator -(const time_point_t &other) const
    {
        return time_point_t { t - other.t };
    }

    time_point_t operator -(double delta) const
    {
        return time_point_t { t - delta };
    }

    time_point_t &operator +=(double delta)
    {
        t += delta;
        return *this;
    }

    bool operator >(const time_point_t &other) const
    {
        return t > other.t;
    }

    bool operator <(const time_point_t &other) const
    {
        return t < other.t;
    }

    double get_real_secs(void) const
    {
        return t.get_real_secs();
    }

    int64_t get_full_secs(void) const
    {
        return t.get_full_secs();
    }

    double get_frac_secs(void) const
    {
        return t.get_frac_secs();
    }
};

template<class T>
double fmod(const time_point_t<T> &t, double x)
{
    return fmod(fmod(t.t.get_full_secs(), x) + fmod(t.t.get_frac_secs(), x), x);
}

template<class T>
bool approx(const time_point_t<T> &t1, const time_point_t<T> &t2)
{
    return fabs((t1 - t2).get_real_secs()) < 1e-6;
}

/** @brief A clock */
class Clock
{
public:
    /** @brief Set the USRP used for clock operations.
     * @param usrp The USRP.
     */
    static void setUSRP(uhd::usrp::multi_usrp::sptr usrp);

    /** @brief Release the USRP used for clock operations. */
    static void releaseUSRP(void);

    /** @brief Give the device clock an offset and rate error when no USRP is
     * attached (full-stack simulation of independent node clocks).
     * @param offset Device time minus host time now (seconds)
     * @param ppm Device clock rate error (parts per million)
     */
    /** Device time is host + offset + 1e-6 ppm (host - epoch), where epoch is
     * the host time of this call, so the clock starts offset by `offset` and
     * drifts at `ppm`. With a USRP attached this has no effect. Defaults
     * (0, 0) keep the host clock.
     */
    static void setSimulatedClock(double offset, double ppm)
    {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        sim_epoch_ = ns*1e-9;
        sim_offset_ = offset;
        sim_rate_ = 1.0 + 1e-6*ppm;
    }

    /** @brief Simulated device clock offset (seconds). */
    static double getSimulatedClockOffset(void) { return sim_offset_; }

    /** @brief Simulated device clock rate error (ppm). */
    static double getSimulatedClockPPM(void) { return 1e6*(sim_rate_ - 1.0); }

    /** @brief Host monotonic time corresponding to a simulated device time. */
    static double simulatedToHost(double device)
    {
        return sim_epoch_ + (device - sim_offset_ - sim_epoch_)/sim_rate_;
    }

    /** @brief Simulated device time corresponding to a host monotonic time. */
    static double hostToSimulated(double host)
    {
        return host + sim_offset_ + (sim_rate_ - 1.0)*(host - sim_epoch_);
    }

protected:
    /** @brief The USRP used for clock operations. */
    static uhd::usrp::multi_usrp::sptr usrp_;

    /** @brief Time zero. */
    static uhd::time_spec_t t0_;

    /** @brief Simulated device clock (no USRP): offset and rate. */
    static double sim_offset_;
    static double sim_rate_;
    static double sim_epoch_;

    /** @brief Get the current UHD time. */
    static uhd::time_spec_t getTimeNow() noexcept
    {
        // Without an attached USRP (simulation, PHY tests) use the host's
        // monotonic clock; previously this dereferenced a null pointer. With a
        // USRP the device time is used exactly as before.
        if (!usrp_) {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            uhd::time_spec_t host(static_cast<int64_t>(ns / 1000000000), static_cast<double>(ns % 1000000000) * 1e-9);

            // Independent simulated node clocks (setSimulatedClock); exact
            // host time by default.
            if (sim_offset_ == 0.0 && sim_rate_ == 1.0)
                return host;
            return host + (sim_offset_ + (sim_rate_ - 1.0)*(host.get_real_secs() - sim_epoch_));
        }
        while (true) {
            try {
                return usrp_->get_time_now();
            } catch (uhd::io_error &err) {
                fprintf(stderr, "USRP: get_time_now: %s", err.what());
            }
        }
    }

    /** @brief Set the current UHD time.
     * @param now Current UHD time
     */
    static void setTimeNow(const uhd::time_spec_t &now) noexcept
    {
        usrp_->set_time_now(now);
    }
};

/** @brief A monotonic clock */
class MonoClock : public Clock
{
private:
    struct mono_tag;

public:
    using duration   = std::chrono::seconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = time_point_t<mono_tag>;

    static const bool is_steady = true;

    /** @brief Get the current time. Guaranteed to be monotonic. */
    static time_point now() noexcept
    {
        return time_point { getTimeNow() };
    }
};

/** @brief A wall-clock clock */
class WallClock : public Clock
{
private:
    struct wall_tag;

public:
    using duration   = std::chrono::seconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = time_point_t<wall_tag>;

    static const bool is_steady = false;

    /** @brief Get time 0 for purposes of linear fit. */
    MonoClock::time_point getTimeZero(void)
    {
        return MonoClock::time_point { t0_ };
    }

    /** @brief Get time offset. */
    MonoClock::time_point getTimeOffset(void)
    {
        return MonoClock::time_point { offset_ };
    }

    /** @brief Set time offset. */
    void setTimeOffset(const MonoClock::time_point &offset)
    {
        offset_ = offset.t;
    }

    /** @brief Get skew. */
    double getSkew(void)
    {
        return skew_;
    }

    /** @brief set skew. */
    void setSkew(double skew)
    {
        skew_ = skew;
    }

    /** @brief Get the current wall-clock time. */
    static time_point now() noexcept
    {
        uhd::time_spec_t now = getTimeNow();

        return time_point { t0_ + offset_ + skew_*(now - t0_).get_real_secs() };
    }

    /** @brief Return the monotonic time corresponding to wall-clock time. */
    static MonoClock::time_point to_mono_time(const time_point &t) noexcept
    {
        return MonoClock::time_point { t0_ + (t.t - t0_ - offset_).get_real_secs() / skew_ };
    }

    /** @brief Return the wall-clock time corresponding to monotonic time. */
    static time_point to_wall_time(const MonoClock::time_point &t) noexcept
    {
        return time_point { t0_ + offset_ + skew_*(t.t - t0_).get_real_secs() };
    }

private:
    /** @brief The offset between the USRP's clock and wall-clock time. */
    static uhd::time_spec_t offset_;

    /** @brief Clock skew. */
    static double skew_;
};

#endif /* CLOCK_H_ */
