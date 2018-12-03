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
        for (auto it = window_.begin(); it != window_.end() && it->first + twindow_ < t; it = window_.begin()) {
            sum_ -= it->second;
            window_.pop_front();
        }
    }
};

#endif /* TIMEWINDOWESTIMATOR_HH_ */
