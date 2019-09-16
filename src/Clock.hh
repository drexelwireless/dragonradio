#ifndef CLOCK_H_
#define CLOCK_H_

#include <chrono>
#include <memory>

#include <uhd/usrp/multi_usrp.hpp>

#include "Seq.hh"

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

/** @brief A monotonic clock */
class MonoClock
{
private:
    struct mono_tag;

public:
    using duration   = std::chrono::seconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = time_point_t<mono_tag>;

    static const bool is_steady = true;

    /** @brief Get time 0, for purposes of linear fit. */
    MonoClock::time_point getTimeZero(void)
    {
        return time_point { t0_ };
    }

    /** @brief Get the current time. Guaranteed to be monotonic. */
    static time_point now() noexcept
    {
        while (true) {
            try {
                return time_point { usrp_->get_time_now() };
            } catch (uhd::io_error &err) {
                fprintf(stderr, "USRP: get_time_now: %s", err.what());
            }
        }
    }

protected:
    /** @brief Time zero, for purposes of linear fit. */
    static uhd::time_spec_t t0_;

    /** @brief The USRP used for clock operations. */
    static uhd::usrp::multi_usrp::sptr usrp_;
};

/** @brief A wall-clock clock */
class Clock : public MonoClock
{
private:
    struct wall_tag;

public:
    using duration   = std::chrono::seconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = time_point_t<wall_tag>;

    static const bool is_steady = false;

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
        while (true) {
            try {
                uhd::time_spec_t now = usrp_->get_time_now();

                return time_point { t0_ + offset_ + skew_*(now - t0_).get_real_secs() };
            } catch (uhd::io_error &err) {
                fprintf(stderr, "USRP: get_time_now: %s", err.what());
            }
        }
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

    /** @brief Set the USRP used for clock operations.
     * @param usrp The USRP.
     */
    static void setUSRP(uhd::usrp::multi_usrp::sptr usrp);

    /** @brief Release the USRP used for clock operations. */
    static void releaseUSRP(void);

private:
    /** @brief The offset between the USRP's clock and wall-clock time. */
    static uhd::time_spec_t offset_;

    /** @brief Clock skew. */
    static double skew_;
};

#endif /* CLOCK_H_ */
