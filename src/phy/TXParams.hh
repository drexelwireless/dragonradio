#ifndef TXPARAMS_HH_
#define TXPARAMS_HH_

#include <complex>

#include <liquid/liquid.h>

#include "IQBuffer.hh"
#include "phy/MCS.hh"
#include "stats/Estimator.hh"

/** @brief PHY TX parameters. */
struct TXParams {
    TXParams(const MCS &mcs)
      : mcs(mcs)
      , g_0dBFS(1.0f)
      , auto_soft_tx_gain_clip_frac(0.999f)
      , nestimates_0dBFS(0)
    {
    }

    TXParams()
      : g_0dBFS(1.0f)
      , auto_soft_tx_gain_clip_frac(0.999f)
      , nestimates_0dBFS(0)
    {
    }

    /** @brief Modulation and coding scheme */
    MCS mcs;

    /** @brief Multiplicative TX gain necessary for 0dBFS. */
    Mean<float> g_0dBFS;

    /** @brief Fraction of unclipped IQ values. Defaults to 0.999. */
    /** This sets the fraction of values guaranteed to be unclipped when the
     * 0dBFS soft TX gain is automatically determined. For example, a value of
     * 0.99 ensures that 99% of the values will fall below 1, i.e., the
     * 99th percentile is unclipped.
     */
    float auto_soft_tx_gain_clip_frac;

    /** @brief Number of samples to take to estimate g_0dBFS. */
    unsigned nestimates_0dBFS;

    /** @brief Get soft TX gain (dB). */
    float getSoftTXGain0dBFS(void) const
    {
        return 20.0*logf(g_0dBFS.getValue())/logf(10.0);
    }

    /** @brief Set soft TX gain (dBFS).
     * @param dB The soft gain (dBFS).
     */
    void setSoftTXGain0dBFS(float dB)
    {
        g_0dBFS.reset(powf(10.0f, dB/20.0f));
    }

    /** @brief Recalculate the 0dBFS estimate
     * @param nsamples The number of samples used to estimate 0dBFS
     */
    void recalc0dBFSEstimate(unsigned nsamples)
    {
        g_0dBFS.reset(g_0dBFS.getValue());
        nestimates_0dBFS = nsamples;
    }

    /** @brief Calculate soft TX gain necessary for 0 dBFS.
     * @param tx_params The PHY TX parameters we are measuring against.
     * @param buf The IQ buffer for which we are calculating soft gain.
     */
    void autoSoftGain0dBFS(float g, std::shared_ptr<IQBuf> buf);
};

#endif /* TXPARAMS_HH_ */
