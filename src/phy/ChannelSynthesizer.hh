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

/** @brief A single-channel synthesizer. */
class ChannelSynthesizer : public Synthesizer
{
public:
    using container_type = std::list<std::unique_ptr<ModPacket>>;

    ChannelSynthesizer(const std::vector<PHYChannel> &channels,
                       double tx_rate,
                       unsigned nthreads)
      : Synthesizer(channels, tx_rate, nthreads+1)
      , enabled_(true)
      , nthreads_(nthreads)
    {
    }

    virtual ~ChannelSynthesizer();

    /** @brief Get high-water mark. */
    std::optional<size_t> getHighWaterMark(void) const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        return high_water_mark_;
    }

    /** @brief Set high-water mark. */
    void setHighWaterMark(std::optional<size_t> high_water_mark)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        high_water_mark_ = high_water_mark;
    }

    /** @brief Is the queue enabled? */
    bool isEnabled(void) const
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            return enabled_;
        }
    }

    /** @brief Enable the queue. */
    void enable(void)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            enabled_ = true;
        }

        producer_cv_.notify_all();
        consumer_cv_.notify_all();
    }

    /** @brief Disable the queue. */
    void disable(void)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            enabled_ = false;
        }

        producer_cv_.notify_all();
        consumer_cv_.notify_all();
    }

    /** @brief Pop all available modulated packets.
     * @return A TXRecord containing packets to transmit
     */
    TXRecord try_pop(void)
    {
        TXRecord txrecord;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            if (!enabled_)
                return txrecord;

            txrecord = std::move(txrecord_);
        }

        producer_cv_.notify_all();

        return txrecord;
    }

    /** @brief Pop at least one packet.
     * @return A TXRecord containing packets to transmit
     */
    TXRecord pop(void)
    {
        TXRecord txrecord;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            consumer_cv_.wait(lock, [&]{ return !enabled_ || txrecord_.nsamples > 0; });

            if (!enabled_ || txrecord_.nsamples == 0)
                return txrecord;

            txrecord = std::move(txrecord_);
        }

        producer_cv_.notify_all();

        return txrecord;
    }

    /** @brief Pop at least one packet with a timeout.
     * @param timeout_time The time at which to stop waiting for packets
     * @return A TXRecord containing packets to transmit
     */
    template <class Clock, class Duration>
    TXRecord pop_until(const std::chrono::time_point<Clock, Duration>& timeout_time)
    {
        TXRecord txrecord;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            consumer_cv_.wait_for(lock, timeout_time - Clock::now(), [&]{ return !enabled_ || txrecord_.nsamples > 0; });

            if (!enabled_ || txrecord_.nsamples == 0)
                return txrecord;

            txrecord = std::move(txrecord_);
        }

        producer_cv_.notify_all();

        return txrecord;
    }

    void stop(void) override;

protected:
    /** @brief Index of channel we should synthesize. */
    std::optional<size_t> chanidx_;

    /** @brief Mutex for waking demodulators. */
    mutable std::mutex queue_mutex_;

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
    bool push(std::unique_ptr<ModPacket>&& mpkt)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            mpkt->start = txrecord_.nsamples;

            txrecord_.nsamples += mpkt->nsamples;
            txrecord_.iqbufs.push_back(std::move(mpkt->samples));
            txrecord_.mpkts.push_back(std::move(mpkt));
        }

        consumer_cv_.notify_one();

        return true;
    }

    /** @brief Can we push a modulated packet? */
    /** The queue mutex must be held by the caller */
    bool can_push(void) const
    {
        return !high_water_mark_ || txrecord_.nsamples < *high_water_mark_;
    }

    /** @brief Wait until we can push a packet */
    bool wait_until_can_push(void)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        producer_cv_.wait(lock, [&]{ return needs_sync() || can_push(); });

        return can_push();
    }

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* CHANNELSYNTHESIZER_HH_ */
