#include "phy/Gain.hh"

float autoSoftGain0dBFS(IQBuf& buf, float clip_frac)
{
    size_t n = 2*buf.size();
    float* f = reinterpret_cast<float*>(buf.data());
    auto   power = std::unique_ptr<float[]>(new float[2*n]);

    for (size_t i = 0; i < n; ++i) {
        float temp = f[i];

        power[i] = temp*temp;
    }

    std::sort(power.get(), power.get() + n);

    size_t max_n = clip_frac*n;

    if (max_n < 0)
        max_n = 0;
    else if (max_n >= n)
        max_n = n - 1;

    float max_amp2 = power[max_n];

    return sqrtf(1/max_amp2);
}
