// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FILTER_H_
#define FILTER_H_

#include <vector>

template <class I, class O>
class Filter
{
public:
    Filter() = default;
    virtual ~Filter() = default;

    /** @brief Get filter group delay */
    virtual float getGroupDelay(float fc) const = 0;

    /** @brief Reset filter state */
    virtual void reset(void) = 0;

    /** @brief Execute the filter */
    virtual void execute(const I *in, O *out, size_t n) = 0;
};

template <class I, class O, class C>
class FIR : public Filter<I,O>
{
public:
    FIR() = default;
    virtual ~FIR() = default;

    /** @brief Get filter delay */
    virtual float getDelay(void) const = 0;

    /** @brief Get taps */
    virtual const std::vector<C> &getTaps(void) const = 0;

    /** @brief Set taps */
    virtual void setTaps(const std::vector<C> &taps) = 0;
};

template <class I, class O, class C>
class IIR : public Filter<I,O>
{
public:
    IIR() = default;
    virtual ~IIR() = default;
};

#endif /* FILTER_H_ */
