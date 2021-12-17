// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef ESTIMATOR_HH_
#define ESTIMATOR_HH_

#include <assert.h>

#include <vector>

/** @brief A statistical estimator */
template<class T>
class Estimator {
public:
    virtual ~Estimator() = default;

    /** @brief Does the estimator have a value? */
    virtual operator bool() const = 0;

    /** @brief Return the value of the estimator */
    virtual T operator *() const
    {
        return *value();
    }

    /** @brief Return the value of the estimator */
    virtual std::optional<T> value(void) const = 0;

    /** @brief Return the value of the estimator or a default */
    virtual T value_or(T&& default_value) const = 0;

    /** @brief Return the number of samples used in the estimate */
    virtual size_t size(void) const = 0;

    /** @brief Update the estimator with a new value */
    virtual void update(T x) = 0;
};

/** @brief Estimate a value by calculating a mean */
template<class T>
class Mean : public Estimator<T> {
public:
    Mean()
      : value_(0.0)
      , nsamples_(0)
    {
    }

    explicit Mean(T initial_value)
      : value_(initial_value)
      , nsamples_(0)
    {
    }

    operator bool() const override
    {
        return true;
    }

    T operator *() const override
    {
        return value_;
    }

    std::optional<T> value(void) const override
    {
        return value_;
    }

    T value_or(T&& default_value) const override
    {
        return value_;
    }

    size_t size(void) const override
    {
        return nsamples_;
    }

    /** @brief Reset the estimator with an initial value */
    void reset(T x)
    {
        value_ = x;
        nsamples_ = 0;
    }

    void update(T x) override
    {
        if (nsamples_ == 0) {
            value_ = x;
            nsamples_ = 1;
        } else {
            value_ = (value_*nsamples_ + x)/(nsamples_ + 1);
            ++nsamples_;
        }
    }

    /** @brief Remove a value used to estimate the mean */
    /** 'remove' does not check that the value being removed from the estimate
     * was used to update the estimate in the past.
     */
    void remove(T x)
    {
        assert(nsamples_ != 0);

        if (nsamples_ == 1)
            nsamples_ = 0;
        else {
            value_ = (value_*nsamples_ - x)/(nsamples_ - 1);
            --nsamples_;
        }
    }

private:
    T value_;
    unsigned nsamples_;
};

/** @brief Estimate a value by calculating a mean over a window of values */
template<class T>
class WindowedMean : public Estimator<T> {
public:
    using size_type = typename std::vector<T>::size_type;

    explicit WindowedMean(size_type n)
      : window_(n, 0)
      , i_(0)
      , sum_(0)
    {
        assert(n > 0);
    }

    size_type getWindowSize(void) const
    {
        return window_.size();
    }

    void setWindowSize(size_type n)
    {
        window_.resize(n);
        std::fill(window_.begin(), window_.end(), 0);
        i_ = 0;
        sum_ = 0;
    }

    operator bool() const override
    {
        return window_.size() != 0 && i_ >= window_.size();
    }

    T operator *() const override
    {
        return sum_/std::min(i_, window_.size());
    }

    std::optional<T> value(void) const override
    {
        if (window_.size() != 0 && i_ >= window_.size())
            return sum_/std::min(i_, window_.size());
        else
            return std::nullopt;
    }

    T value_or(T&& default_value) const override
    {
        if (window_.size() != 0 && i_ >= window_.size())
            return sum_/std::min(i_, window_.size());
        else
            return default_value;
    }

    size_t size(void) const override
    {
        return std::min(i_, window_.size());
    }

    void reset(void)
    {
        std::fill(window_.begin(), window_.end(), 0);
        i_ = 0;
    }

    void update(T x) override
    {
        if (i_ == 0)
            sum_ = x;
        else
            sum_ = sum_ - window_[i_ % window_.size()] + x;

        window_[i_++ % window_.size()] = x;
    }

private:
    std::vector<T> window_;
    size_type      i_;
    T              sum_;
};

/** @brief Estimate a value by calculating an exponential moving average */
/** The EMA estimator updates an exponetially weighted moving average with a
 * weight alpha. Optionally, it can make an estimate using an average until
 * mean_until samples have been collected. As a guideline, choosing alpha to be
 * 2/(n+1) means the first n data points "will represent about 86% of the total
 * weight in the calculation EMA."
 *
 * See: https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average
 */
template<class T>
class EMA : public Estimator<T> {
public:
    /** @brief Create and EMA estimator
     * @param alpha The weight used to update the EMA
     */
    explicit EMA(T alpha)
      : value_(0.0)
      , nsamples_(0)
      , mean_until_(0)
      , alpha_(alpha)
    {
    }

    /** @brief Create and EMA estimator
     * @param alpha The weight used to update the EMA
     * @param initial_value The initial value of the EMA
     * @param mean_until The number of samples before which we estimate using
     * a mean instead of an EMA
     */
    EMA(T alpha, T initial_value, unsigned mean_until)
      : value_(initial_value)
      , nsamples_(0)
      , mean_until_(mean_until)
      , alpha_(alpha)
    {
    }

    EMA() = delete;

    T getValue(void) const override
    {
        return value_;
    }

    unsigned getNSamples(void) const override
    {
        return nsamples_;
    }

    void reset(T x) override
    {
        value_ = x;
        nsamples_ = 0;
    }

    void update(T x) override
    {
        if (nsamples_ == 0) {
            value_ = x;
            nsamples_ = 1;
        } else if (nsamples_ < mean_until_) {
            value_ = (value_*nsamples_ + x)/(nsamples_ + 1);
            ++nsamples_;
        } else {
            value_ = value_ + alpha_*(x - value_);
            ++nsamples_;
        }
    }

private:
    T value_;
    unsigned nsamples_;
    unsigned mean_until_;
    T alpha_;
};

#endif /* ESTIMATOR_HH_ */
