#include "RadioConfig.hh"
#include "Logger.hh"
#include "phy/TXParams.hh"

void TXParams::autoSoftGain0dBFS(float g, std::shared_ptr<IQBuf> buf)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        if (nestimates_0dBFS_ > 0)
            --nestimates_0dBFS_;
        else
            return;
    }

    size_t n = buf->size();

    // This should never happen, but just in case...
    if (n == 0)
        return;

    std::complex<float>* f = buf->data();
    auto                 power = std::unique_ptr<float[]>(new float[n]);

    for (size_t i = 0; i < n; ++i) {
        std::complex<float> temp = f[i];

        power[i] = temp.real()*temp.real() + temp.imag()*temp.imag();
    }

    std::sort(power.get(), power.get() + n);

    size_t max_n = getAutoSoftTXGainClipFrac()*n;

    if (max_n < 0)
        max_n = 0;
    else if (max_n >= n)
        max_n = n - 1;

    float max_amp2 = power[max_n];

    // Avoid division by 0!
    if (max_amp2 == 0.0)
        return;

    // XXX Should I^2 + Q^2 = 1.0 or 2.0?
    float g_estimate = sqrtf(1.0/max_amp2);

    // g is the gain multiplier used to produce the IQ samples
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        g_0dBFS_estimate_.update(g*g_estimate);
        g_0dBFS_.store(g_0dBFS_estimate_.getValue(), std::memory_order_relaxed);
    }

    logEvent("AMC: updated auto-gain %0.1f", (double) getSoftTXGain0dBFS());
}
