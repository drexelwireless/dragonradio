#ifndef TXPARAMS_HH_
#define TXPARAMS_HH_

#include <complex>
#include <mutex>
#include <shared_mutex>

#include <liquid/liquid.h>

#include "IQBuffer.hh"
#include "phy/MCS.hh"
#include "stats/Estimator.hh"

/** @brief PHY TX parameters. */
struct TXParams {
    TXParams(const MCS &mcs, std::optional<double> evm_threshold = std::nullopt)
      : mcs(mcs)
      , evm_threshold(evm_threshold)
      , g_0dBFS_(1.0f)
      , auto_soft_tx_gain_clip_frac_(0.999f)
      , nestimates_0dBFS_(0)
      , g_0dBFS_estimate_(1.0f)
    {
    }

    TXParams(const TXParams &other)
    {
        std::shared_lock<std::shared_mutex> lock(other.mutex_);

        mcs = other.mcs;
        evm_threshold = other.evm_threshold;
        g_0dBFS_.store(other.g_0dBFS_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        auto_soft_tx_gain_clip_frac_.store(other.auto_soft_tx_gain_clip_frac_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        nestimates_0dBFS_ = other.nestimates_0dBFS_;
        g_0dBFS_estimate_ = other.g_0dBFS_estimate_;
    }

    TXParams(TXParams &&other)
    {
        mcs = std::move(other.mcs);
        evm_threshold = std::move(other.evm_threshold);
        g_0dBFS_.store(other.g_0dBFS_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        auto_soft_tx_gain_clip_frac_.store(other.auto_soft_tx_gain_clip_frac_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        nestimates_0dBFS_ = other.nestimates_0dBFS_;
        g_0dBFS_estimate_ = std::move(other.g_0dBFS_estimate_);
    }

    TXParams() = delete;

    TXParams& operator =(const TXParams &other)
    {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> this_lock(mutex_, std::defer_lock);
            std::shared_lock<std::shared_mutex> other_lock(other.mutex_, std::defer_lock);

            std::lock(this_lock, other_lock);

            mcs = other.mcs;
            evm_threshold = other.evm_threshold;
            g_0dBFS_.store(other.g_0dBFS_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            auto_soft_tx_gain_clip_frac_.store(other.auto_soft_tx_gain_clip_frac_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            nestimates_0dBFS_ = other.nestimates_0dBFS_;
            g_0dBFS_estimate_ = other.g_0dBFS_estimate_;
        }

        return *this;
    }

    TXParams& operator =(TXParams &&other)
    {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock(mutex_);

            mcs = std::move(other.mcs);
            evm_threshold = std::move(other.evm_threshold);
            g_0dBFS_.store(other.g_0dBFS_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            auto_soft_tx_gain_clip_frac_.store(other.auto_soft_tx_gain_clip_frac_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            nestimates_0dBFS_ = other.nestimates_0dBFS_;
            g_0dBFS_estimate_ = std::move(other.g_0dBFS_estimate_);
        }

        return *this;
    }

    /** @brief Modulation and coding scheme */
    MCS mcs;

    /** @brief EVM required for this MCS */
    std::optional<double> evm_threshold;

    /** @brief Get the fraction of unclipped IQ values. Defaults to 0.999. */
    float getAutoSoftTXGainClipFrac(void) const
    {
        return auto_soft_tx_gain_clip_frac_.load(std::memory_order_relaxed);
    }

    /** @brief Set the fraction of unclipped IQ values. Defaults to 0.999. */
    /** This sets the fraction of values guaranteed to be unclipped when the
     * 0dBFS soft TX gain is automatically determined. For example, a value of
     * 0.99 ensures that 99% of the values will fall below 1, i.e., the
     * 99th percentile is unclipped.
     */
    void setAutoSoftTXGainClipFrac(float frac)
    {
        auto_soft_tx_gain_clip_frac_.store(frac, std::memory_order_relaxed);
    }

    /** @brief Get soft TX gain (multiplicative factor). */
    float getSoftTXGain(void) const
    {
        return g_0dBFS_.load(std::memory_order_relaxed);
    }

    /** @brief Set soft TX gain (multiplicative factor).
     * @param dB The soft gain (multiplicative factor).
     */
    void setSoftTXGain(float g)
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        g_0dBFS_estimate_.reset(g);
        g_0dBFS_.store(g_0dBFS_estimate_.getValue(), std::memory_order_relaxed);
    }

    /** @brief Get soft TX gain (dB). */
    float getSoftTXGain0dBFS(void) const
    {
        return 20.0*logf(g_0dBFS_.load(std::memory_order_relaxed))/logf(10.0);
    }

    /** @brief Set soft TX gain (dBFS).
     * @param dB The soft gain (dBFS).
     */
    void setSoftTXGain0dBFS(float dB)
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        g_0dBFS_estimate_.reset(powf(10.0f, dB/20.0f));
        g_0dBFS_.store(g_0dBFS_estimate_.getValue(), std::memory_order_relaxed);
    }

    /** @brief Recalculate the 0dBFS estimate
     * @param nsamples The number of samples used to estimate 0dBFS
     */
    void recalc0dBFSEstimate(unsigned nsamples)
    {
        std::lock_guard<std::shared_mutex> lock(mutex_);

        g_0dBFS_estimate_.reset(g_0dBFS_estimate_.getValue());
        g_0dBFS_.store(g_0dBFS_estimate_.getValue(), std::memory_order_relaxed);
        nestimates_0dBFS_ = nsamples;
    }

    /** @brief Do we need to calculate auto-gain? */
    bool needCalcAutoSoftGain0dBFS(void) const
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);

        return nestimates_0dBFS_ > 0;
    }

    /** @brief Calculate soft TX gain necessary for 0 dBFS.
     * @param tx_params The PHY TX parameters we are measuring against.
     * @param buf The IQ buffer for which we are calculating soft gain.
     */
    void autoSoftGain0dBFS(float g, std::shared_ptr<IQBuf> buf);

private:
    /** @brief Multiplicative TX gain necessary for 0dBFS. */
    std::atomic<float> g_0dBFS_;

    /** @brief Fraction of unclipped IQ values. Defaults to 0.999. */
    std::atomic<float> auto_soft_tx_gain_clip_frac_;

    /** @brief Mutex protecting private members. */
    mutable std::shared_mutex mutex_;

    /** @brief Number of samples to take to estimate g_0dBFS. */
    unsigned nestimates_0dBFS_;

    /** @brief Estimate of multiplicative TX gain necessary for 0dBFS. */
    Mean<float> g_0dBFS_estimate_;
};

#endif /* TXPARAMS_HH_ */
