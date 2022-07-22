// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MULTICHANNELSYNTHESIZER_H_
#define MULTICHANNELSYNTHESIZER_H_

#include "dsp/FDResampler.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief A frequency-domain, per-channel synthesizer. */
class MultichannelSynthesizer : public Synthesizer
{
public:
    using Resampler = dragonradio::signal::FDResampler<C>;

    static constexpr auto P = Resampler::P;
    static constexpr auto N = Resampler::N;
    static constexpr auto L = Resampler::L;
    static constexpr auto O = Resampler::O;

    MultichannelSynthesizer(const std::vector<PHYChannel> &channels,
                            double tx_rate,
                            size_t nthreads);
    virtual ~MultichannelSynthesizer();

    std::optional<size_t> getHighWaterMark(void) const override;

    void setHighWaterMark(std::optional<size_t> high_water_mark) override;

    bool isEnabled(void) const override;

    void enable(void) override;

    void disable(void) override;

    TXRecord try_pop(void) override;

    TXRecord pop(void) override;

    TXRecord pop_for(const std::chrono::duration<double>& rel_time) override;

    void push_slot(const WallClock::time_point& when, size_t slot, ssize_t prev_oversample) override;

    TXSlot pop_slot(void) override;

    void stop(void) override;

private:
    struct Slot;
    class MultichannelModulator;

    /** @brief Mutex for current slot */
    mutable std::mutex curslot_mutex_;

    /** @brief Condition variable for packet producers */
    std::condition_variable producer_cv_;

    /** @brief Condition variable for packet consumers */
    std::condition_variable consumer_cv_;

    /** @brief Maximum number of IQ samples the queue may contain */
    std::optional<size_t> high_water_mark_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Mutexes for modulators. */
    std::vector<std::mutex> mod_mutexes_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<MultichannelModulator>> mods_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief OLS time domain converter */
    Resampler::ToTimeDomain timedomain_;

    /** @brief Gain necessary to compensate for simultaneous transmission */
    float g_multichan_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Push a modulated packet onto a slot
     * @param mpkt The modulated packet
     * @param slot The slot
     * @return true if the modulated packets was successfully pushed, false otherwise
     */
    bool push(std::unique_ptr<ModPacket>& mpkt, Slot& slot);

    /** @brief Push a slot corresponding to the current time */
    void push_slot(void);

    /** @brief Close and finalize a slot */
    void closeAndFinalize(Slot& slot);

    /** @brief Finalize a slot */
    void finalize(Slot& slot);

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);

    void reconfigure(void) override;

    void wake_dependents() override;
};

#endif /* MULTICHANNELSYNTHESIZER_H_ */
