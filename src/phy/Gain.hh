// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef GAIN_H_
#define GAIN_H_

#include <math.h>

#include <atomic>

class Gain {
public:
    Gain(void)
      : g_(1.0)
    {
    }

    ~Gain() = default;

    Gain& operator =(const Gain &other)
    {
        if (this != &other)
            g_.store(other.g_.load(std::memory_order_relaxed), std::memory_order_relaxed);

        return *this;
    }

    float getLinearGain(void) const
    {
        return g_.load(std::memory_order_relaxed);
    }

    void setLinearGain(float g)
    {
        g_.store(g, std::memory_order_relaxed);
    }

    float getDbGain(void) const
    {
        return 20.0*logf(g_.load(std::memory_order_relaxed))/logf(10.0);
    }

    void setDbGain(float dB)
    {
        g_.store(powf(10.0f, dB/20.0f), std::memory_order_relaxed);
    }

protected:
    std::atomic<float> g_;
};

#endif /* GAIN_H_ */
