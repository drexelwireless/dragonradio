// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TIMEWINDOWESTIMATOR_HH_
#define TIMEWINDOWESTIMATOR_HH_

#include <deque>
#include <limits>

#include "Estimator.hh"

/** @brief A statistical estimator over a time window */
template<class Clock, class T>
class TimeWindowEstimator : public Estimator<T> {
public:
    using entry = std::pair<typename Clock::time_point, T>;

    explicit TimeWindowEstimator(double twindow=1.0)
      : twindow_(twindow)
    {
    }

    virtual ~TimeWindowEstimator() = default;

    /** @brief Get the current time window */
    virtual double getTimeWindow(void) const
    {
        return twindow_;
    }

    /** @brief Set the current time window */
    virtual void setTimeWindow(double twindow)
    {
        twindow_ = twindow;
    }

    /** @brief Get start of window */
    virtual typename Clock::time_point getTimeWindowStart() const
    {
        if (window_.size() == 0)
            return typename Clock::time_point();
        else
            return window_.begin()->first;
    }

    /** @brief Get end of window */
    virtual typename Clock::time_point getTimeWindowEnd() const
    {
        if (window_.size() == 0)
            return typename Clock::time_point();
        else
            return window_.rbegin()->first;
    }

    virtual unsigned getNSamples(void) const override
    {
        return window_.size();
    }

    virtual void reset(T x) override
    {
        window_.clear();
    }

    virtual void update(T x) override
    {
        update(Clock::now(), x);
    }

    /** @brief Update the estimator with a new value */
    virtual void update(typename Clock::time_point t, T x) = 0;

protected:
    /** @brief Time window (sec) */
    double twindow_;

    /** @brief Values in our window */
    mutable std::deque<entry> window_;
};

/** @brief Compute mean over a time window */
template<class Clock, class T>
class TimeWindowMean : public TimeWindowEstimator<Clock, T> {
public:
    using TimeWindowEstimator<Clock, T>::twindow_;
    using TimeWindowEstimator<Clock, T>::window_;

    explicit TimeWindowMean(double twindow=1.0)
      : TimeWindowEstimator<Clock, T>(twindow)
      , sum_(0)
    {
    }

    virtual ~TimeWindowMean() = default;

    virtual T getValue(void) const override
    {
        purge(Clock::now());
        if (window_.size() == 0)
            return std::numeric_limits<double>::quiet_NaN();
        else
            return sum_/static_cast<T>(window_.size());
    }

    virtual void reset(T x) override
    {
        TimeWindowEstimator<Clock, T>::reset(x);
        sum_ = x;
    }

    void update(typename Clock::time_point t, T x) override
    {
        purge(t);

        sum_ += x;
        window_.push_back(std::make_pair(t, x));
    }

protected:
    /** @brief Sum of values in our window */
    mutable T sum_;

    /** @brief Remove elements outside the time window */
    void purge(typename Clock::time_point t) const
    {
        // Pop first element of buffer as long as it falls before our window
        while (!window_.empty() && window_.front().first + twindow_ < t) {
            sum_ -= window_.front().second;
            window_.pop_front();
        }
    }
};

/** @brief Compute mean value *per second* over a time window */
template<class Clock, class T>
class TimeWindowMeanRate : public TimeWindowMean<Clock, T> {
public:
    using TimeWindowEstimator<Clock, T>::twindow_;
    using TimeWindowEstimator<Clock, T>::window_;
    using TimeWindowMean<Clock, T>::sum_;
    using TimeWindowMean<Clock, T>::purge;

    explicit TimeWindowMeanRate(double twindow=1.0)
      : TimeWindowMean<Clock, T>(twindow)
    {
    }

    virtual ~TimeWindowMeanRate() = default;

    virtual T getValue(void) const override
    {
        purge(Clock::now());
        return sum_/twindow_;
    }
};

/** @brief Compute minimum over a time window */
template<class Clock, class T>
class TimeWindowMin : public TimeWindowEstimator<Clock, T> {
public:
    using TimeWindowEstimator<Clock, T>::twindow_;
    using TimeWindowEstimator<Clock, T>::window_;

    explicit TimeWindowMin(double twindow=1.0)
      : TimeWindowEstimator<Clock, T>(twindow)
      , min_(0)
    {
    }

    virtual ~TimeWindowMin() = default;

    virtual T getValue(void) const override
    {
        purge(Clock::now());

        if (window_.size() == 0)
            return std::numeric_limits<double>::quiet_NaN();
        else
            return min_;
    }

    virtual void reset(T x) override
    {
        TimeWindowEstimator<Clock, T>::reset(x);
        min_ = x;
    }

    void update(typename Clock::time_point t, T x) override
    {
        purge(t);

        if (x <= min_) {
            min_ = x;
            window_.clear();
        }

        window_.push_back(std::make_pair(t, x));
    }

protected:
    /** @brief Minimum value in our window */
    mutable T min_;

    /** @brief Remove elements outside the time window */
    void purge(typename Clock::time_point t) const
    {
        bool recalc = false;

        // Pop first element of buffer as long as it falls before our window.
        while (!window_.empty() && window_.front().first + twindow_ < t) {
            recalc = true;
            window_.pop_front();
        }

        // Recalculate minimum
        auto it = window_.begin();

        if (recalc && it != window_.end()) {
            min_ = it->second;

            ++it;

            for (; it != window_.end(); ++it) {
                if (it->second <= min_)
                    min_ = it->second;
            }

            for (auto it = window_.begin(); it != window_.end() && it->second > min_; it = window_.begin())
                window_.pop_front();
        }
    }
};

/** @brief Compute maximum over a time window */
template<class Clock, class T>
class TimeWindowMax : public TimeWindowEstimator<Clock, T> {
public:
    using TimeWindowEstimator<Clock, T>::twindow_;
    using TimeWindowEstimator<Clock, T>::window_;

    explicit TimeWindowMax(double twindow=1.0)
      : TimeWindowEstimator<Clock, T>(twindow)
      , max_(0)
    {
    }

    virtual ~TimeWindowMax() = default;

    virtual T getValue(void) const override
    {
        purge(Clock::now());

        if (window_.size() == 0)
            return std::numeric_limits<double>::quiet_NaN();
        else
            return max_;
    }

    virtual void reset(T x) override
    {
        TimeWindowEstimator<Clock, T>::reset(x);
        max_ = x;
    }

    void update(typename Clock::time_point t, T x) override
    {
        purge(t);

        if (x >= max_) {
            max_ = x;
            window_.clear();
        }

        window_.push_back(std::make_pair(t, x));
    }

protected:
    /** @brief Maximum value in our window */
    mutable T max_;

    /** @brief Remove elements outside the time window */
    void purge(typename Clock::time_point t) const
    {
        bool recalc = false;

        // Pop first element of buffer as long as it falls before our window.
        while (!window_.empty() && window_.front().first + twindow_ < t) {
            recalc = true;
            window_.pop_front();
        }

        // Recalculate maximum
        auto it = window_.begin();

        if (recalc && it != window_.end()) {
            max_ = it->second;

            ++it;

            for (; it != window_.end(); ++it) {
                if (it->second >= max_)
                    max_ = it->second;
            }

            for (auto it = window_.begin(); it != window_.end() && it->second < max_; it = window_.begin())
                window_.pop_front();
        }
    }
};

#endif /* TIMEWINDOWESTIMATOR_HH_ */
