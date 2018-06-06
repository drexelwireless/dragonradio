#ifndef GAIN_HH_
#define GAIN_HH_

#include <liquid/liquid.h>

#include "Estimator.hh"
#include "IQBuffer.hh"

/** @brief PHY TX parameters. */
struct TXParams {
    TXParams(crc_scheme check,
             fec_scheme fec0,
             fec_scheme fec1,
             modulation_scheme ms)
      : check(check)
      , fec0(fec0)
      , fec1(fec1)
      , ms(ms)
      , g_0dBFS(1.0f)
      , soft_tx_gain_clip_frac(0.999f)
      , nestimates_0dBFS(0)
    {
    }

    TXParams()
      : check(LIQUID_CRC_32)
      , fec0(LIQUID_FEC_CONV_V27)
      , fec1(LIQUID_FEC_NONE)
      , ms(LIQUID_MODEM_QPSK)
      , g_0dBFS(1.0f)
      , soft_tx_gain_clip_frac(0.999f)
      , nestimates_0dBFS(0)
    {
    }

    /** @brief CRC */
    crc_scheme check;

    /** @brief FEC0 (inner FEC) */
    fec_scheme fec0;

    /** @brief FEC1 (outer FEC) */
    fec_scheme fec1;

    /** @brief Modulation scheme */
    modulation_scheme ms;

    /** @brief Multiplicative TX gain necessary for 0dBFS. */
    Mean<float> g_0dBFS;

    /** @brief Fraction of unclipped IQ values. Defaults to 0.999. */
    /** This sets the fraction of values guaranteed to be unclipped when the
     * 0dBFS soft TX gain is automatically determined. For example, a value of
     * 0.99 ensures that 99% of the values will fall below 1, i.e., the
     * 99th percentile is unclipped.
     */
    float soft_tx_gain_clip_frac;

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

#endif /* GAIN_HH_ */
