#include "RadioConfig.hh"
#include "Logger.hh"
#include "phy/TXParams.hh"

void TXParams::autoSoftGain0dBFS(float g, std::shared_ptr<IQBuf> buf)
{
    size_t               n = buf->size();
    std::complex<float>* f = buf->data();
    auto                 power = std::unique_ptr<float[]>(new float[n]);

    for (size_t i = 0; i < n; ++i) {
        std::complex<float> temp = f[i];

        power[i] = temp.real()*temp.real() + temp.imag()*temp.imag();
    }

    std::sort(power.get(), power.get() + n);

    size_t max_n = auto_soft_tx_gain_clip_frac*n;

    if (max_n < 0)
        max_n = 0;
    else if (max_n >= n)
        max_n = n - 1;

    float max_amp2 = power[max_n];

    // XXX Should I^2 + Q^2 = 1.0 or 2.0?
    float g_estimate = sqrtf(1.0/max_amp2);

    // g is the gain multiplier used to produce the IQ samples
    g_0dBFS.update(g*g_estimate);

    if (rc.verbose)
        fprintf(stderr, "Updated auto-gain: %0.1f\n", (double) getSoftTXGain0dBFS());
    logEvent("AMC: updated auto-gain %0.1f", (double) getSoftTXGain0dBFS());
}
