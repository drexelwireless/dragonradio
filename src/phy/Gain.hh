#ifndef GAIN_H_
#define GAIN_H_

#include <math.h>

class Gain {
public:
    Gain(void) : g_(1.0)
    {
    }

    ~Gain() = default;

    float getLinearGain(void)
    {
        return g_;
    }

    void setLinearGain(float g)
    {
        g_ = g;
    }

    float getDbGain(void)
    {
        return 20.0*logf(g_)/logf(10.0);
    }

    void setDbGain(float dB)
    {
        g_ = powf(10.0f, dB/20.0f);
    }

protected:
    float g_;
};

#endif /* GAIN_H_ */
