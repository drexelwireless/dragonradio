// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CHANNELSYNTHESIZER_HH_
#define CHANNELSYNTHESIZER_HH_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief Synthesize packets for a single, fixed channel. */
template <class ChannelModulator>
class ChannelSynthesizer : public Synthesizer
{
public:
    ChannelSynthesizer(const std::vector<PHYChannel> &channels,
                       double tx_rate,
                       unsigned nthreads);

    virtual ~ChannelSynthesizer();

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

protected:
    /** @brief Index of channel we should synthesize. */
    std::optional<size_t> chanidx_;

    /** @brief Mutex for waking demodulators. */
    mutable std::mutex queue_mutex_;

    /** @brief Index of slot we should synthesize. */
    std::optional<size_t> slot_;

    /** @brief Deadline of slot we should synthesize. */
    WallClock::time_point slot_deadline_;

    /** @brief Maximum number of samples in a packet */
    std::optional<size_t> max_samples_;

    /** @brief Maximum number of IQ samples the queue may contain */
    std::optional<size_t> high_water_mark_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Queue of modulated packets */
    TXRecord txrecord_;

    /** @brief Producer condition variable */
    std::condition_variable producer_cv_;

    /** @brief Consumer condition variable */
    std::condition_variable consumer_cv_;

    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Push a modulated packet onto the queue */
    bool push(std::unique_ptr<ModPacket>&& mpkt);

    /** @brief Can we push a modulated packet? */
    /** The queue mutex must be held by the caller */
    bool can_push(size_t nsamples) const;

    /** @brief Wait until we can push a packet */
    bool wait_until_can_push(void);

    /** @brief Modulation worker */
    void modWorker(unsigned tid);

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* CHANNELSYNTHESIZER_HH_ */
