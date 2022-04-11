// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CLOCK_H_
#define CLOCK_H_

#include <cmath>
#include <chrono>
#include <memory>

/** @brief A high-resolution representation for time values. */
struct timerep_t
{
    using time_t = int64_t;

    timerep_t() : full_(0), frac_(0.0)
    {
    }

    timerep_t(const timerep_t&) = default;

    timerep_t(double t)
    {
        normalize(0, t);
    }

    timerep_t(time_t full, double frac)
    {
        normalize(full, frac);
    }

    ~timerep_t() = default;

    timerep_t& operator =(const timerep_t&) = default;

#if defined(UHD_VERSION)
    timerep_t(const uhd::time_spec_t& t)
    {
        normalize(t.get_full_secs(), t.get_frac_secs());
    }

    operator uhd::time_spec_t() const
    {
        return uhd::time_spec_t{full_, frac_};
    }
#endif /* defined(UHD_VERSION) */

    time_t get_full(void) const
    {
        return full_;
    }

    double get_frac(void) const
    {
        return frac_;
    }

    operator double() const
    {
        return full_ + frac_;
    }

    bool operator ==(const timerep_t& other)
    {
        return full_ == other.full_ && frac_ == other.frac_;
    }

    bool operator <(const timerep_t& other)
    {
        return full_ < other.full_ || (full_ == other.full_ && frac_ < other.frac_);
    }

    timerep_t& operator +=(const timerep_t& rhs)
    {
        normalize(full_ + rhs.full_, frac_ + rhs.frac_);
        return *this;
    }

    friend timerep_t operator +(timerep_t lhs, const timerep_t& rhs)
    {
        lhs += rhs;
        return lhs;
    }

    timerep_t& operator -=(const timerep_t& rhs)
    {
        normalize(full_ - rhs.full_, frac_ - rhs.frac_);
        return *this;
    }

    friend timerep_t operator -(timerep_t lhs, const timerep_t& rhs)
    {
        lhs -= rhs;
        return lhs;
    }

    timerep_t& operator %=(const timerep_t& rhs)
    {
        double x = static_cast<double>(rhs);

        normalize(0, std::fmod(std::fmod(full_, x) + std::fmod(frac_, x), x));
        return *this;
    }

    friend timerep_t operator %(timerep_t lhs, const timerep_t& rhs)
    {
        lhs %= rhs;
        return lhs;
    }

    friend void swap(timerep_t& lhs, timerep_t& rhs)
    {
        std::swap(lhs.full_, rhs.full_);
        std::swap(lhs.frac_, rhs.frac_);
    }

private:
    /** @brief Integral portion of time representation */
    time_t full_;

    /** @brief Fractional portion of time representation */
    double frac_;

    constexpr void normalize(time_t full, double frac)
    {
        time_t int_frac = frac;

        full_ = full + int_frac;
        frac_ = frac - int_frac;

        if (frac_ < 0) {
            full_ -= 1;
            frac_ += 1;
        }
    }
};

template<>
struct std::chrono::treat_as_floating_point<timerep_t> : std::true_type {};

template<>
struct std::common_type<timerep_t, float>
{
    using type = timerep_t;
};

template<>
struct std::common_type<float, timerep_t>
{
    using type = timerep_t;
};

template<>
struct std::common_type<timerep_t, double>
{
    using type = timerep_t;
};

template<>
struct std::common_type<double, timerep_t>
{
    using type = timerep_t;
};

template<>
struct std::common_type<timerep_t, long double>
{
    using type = timerep_t;
};

template<>
struct std::common_type<long double, timerep_t>
{
    using type = timerep_t;
};

template<>
struct std::common_type<timerep_t, int>
{
    using type = timerep_t;
};

template<>
struct std::common_type<int, timerep_t>
{
    using type = timerep_t;
};

template<>
struct std::common_type<timerep_t, long int>
{
    using type = timerep_t;
};

template<>
struct std::common_type<long int, timerep_t>
{
    using type = timerep_t;
};

template<>
struct std::common_type<timerep_t, unsigned long>
{
    using type = timerep_t;
};

template<>
struct std::common_type<unsigned long, timerep_t>
{
    using type = timerep_t;
};

class MonoClock
{
public:
    using rep                       = timerep_t;
    using period                    = std::ratio<1>;
    using duration                  = std::chrono::duration<rep, period>;
    using time_point                = std::chrono::time_point<MonoClock>;
    static constexpr bool is_steady = true;

    class TimeKeeper
    {
    public:
        virtual ~TimeKeeper() = default;

        virtual time_point now() noexcept = 0;
    };

    static time_point get_t0()
    {
        return t0_;
    }

    static void set_time_keeper(const std::shared_ptr<TimeKeeper>& time_keeper)
    {
        time_keeper_ = time_keeper;
        t0_ = time_keeper->now();
    }

    static void reset_time_keeper()
    {
        time_keeper_.reset();
    }

    static time_point now() noexcept
    {
        if (time_keeper_)
            return time_keeper_->now();
        else
            return time_point{std::chrono::steady_clock::now().time_since_epoch()};
    }

protected:
    static std::shared_ptr<TimeKeeper> time_keeper_;

    static time_point t0_;
};

#if defined(UHD_VERSION)
inline uhd::time_spec_t to_uhd_time(const MonoClock::time_point& t)
{
    return t.time_since_epoch().count();
}

inline MonoClock::time_point from_uhd_time(const uhd::time_spec_t& t)
{
    std::chrono::duration<timerep_t> dur(timerep_t{t});

    return MonoClock::time_point{dur};
}
#endif /* defined(UHD_VERSION) */

class WallClock : public MonoClock
{
public:
    using rep                       = timerep_t;
    using period                    = std::ratio<1>;
    using duration                  = std::chrono::duration<rep, period>;
    using time_point                = std::chrono::time_point<WallClock>;
    static constexpr bool is_steady = false;

    static duration get_offset()
    {
        return offset_;
    }

    static void set_offset(duration offset)
    {
        offset_ = offset;
    }

    static double get_skew()
    {
        return skew_;
    }

    static void set_skew(double skew)
    {
        skew_ = skew;
    }

    static time_point now() noexcept
    {
        return to_wall_time(MonoClock::now());
    }

    /** @brief Return the monotonic time corresponding to wall-clock time. */
    static MonoClock::time_point to_mono_time(const time_point& t) noexcept
    {
        const time_point t0_wall = time_point { t0_.time_since_epoch() };

        return t0_ + (t - t0_wall - offset_)/skew_;
    }

    /** @brief Return the wall-clock time corresponding to monotonic time. */
    static time_point to_wall_time(const MonoClock::time_point& t) noexcept
    {
        const time_point t0_wall = time_point { t0_.time_since_epoch() };

        return t0_wall + offset_ + skew_*(t - t0_);
    }

protected:
    /** @brief The offset between the monotonic clock and wall-clock time. */
    static duration offset_;

    /** @brief Monotonic clock skew. */
    static double skew_;
};

#endif /* CLOCK_H_ */
