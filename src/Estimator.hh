#ifndef ESTIMATOR_HH_
#define ESTIMATOR_HH_

#include <assert.h>

/** @brief A statistical estimator */
template<class T>
class Estimator {
public:
    /** @brief Return the value of the estimator */
    virtual T getValue(void) const = 0;

    /** @brief Return the number of samples used in the estimate */
    virtual unsigned getNSamples(void) const = 0;

    /** @brief Reset the estimator with an initial value */
    virtual void reset(T x) = 0;

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

/** @brief Estimate a value by calculating an exponential moving average */
/** The EMA estimator updates an exponetially weighted moving average with a
 * weight alpha. Optionally, it can make an estimate using an average until
 * mean_until samples have been collected. As a guideline, choosing alpha to be
 * 2/(n+1) means the first n data points "will represent about 86% of teh total
 * weight in the calculation EMA."
 *
 * See: https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average
 */
template<class T>
class EMA : public Estimator<T> {
public:
    /** @brief Create and EMA estimator
     * @param alpha The weight used to update the EMA
     * @param initial_value The initial value of the EMA
     * @param mean_until The number of samples before which we estimate using
     * a mean instead of an EMA
     */
    explicit EMA(T alpha)
      : value_(0.0)
      , nsamples_(0)
      , mean_until_(0)
      , alpha_(alpha)
    {
    }

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
