#ifndef FILTER_H_
#define FILTER_H_

#include <complex>
#include <vector>

#include <liquid/liquid.h>

#include "NCO.hh"

class Filter
{
public:
    Filter() = default;
    virtual ~Filter() = default;

    /** @brief Get filter group delay */
    virtual float getGroupDelay(float fc) = 0;

    /** @brief Reset filter state */
    virtual void reset(void) = 0;

    /** @brief Execute the filter */
    virtual void execute(const std::complex<float> *in, std::complex<float> *out, size_t n) = 0;
};

class FIRFilter : public Filter
{
public:
    FIRFilter() = default;
    virtual ~FIRFilter() = default;

    /** @brief Get filter delay */
    virtual float getDelay(void) = 0;
};

class IIRFilter : public Filter
{
public:
    IIRFilter() = default;
    virtual ~IIRFilter() = default;
};

#endif /* FILTER_H_ */
