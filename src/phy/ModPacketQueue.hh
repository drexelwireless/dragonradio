// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MODPACKETQUEUE_HH_
#define MODPACKETQUEUE_HH_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include "phy/PHY.hh"

/** @brief A queue of modulated packets */
template<class T = std::unique_ptr<ModPacket>, class Container = std::list<T>>
class ModPacketQueue
{
public:
    using container_type = Container;

    ModPacketQueue()
      : done_(false)
      , kicked_(false)
      , nsamples_(0)
    {
    }

    ~ModPacketQueue() = default;

    std::optional<size_t> getHighWaterMark(void) const
    {
        return high_water_mark_;
    }

    void setHighWaterMark(std::optional<size_t> high_water_mark)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        high_water_mark_ = high_water_mark;
    }

    void stop(void)
    {
        done_ = true;

        producer_cond_.notify_all();
        consumer_cond_.notify_all();
    }

    size_t try_pop(container_type &mpkts, size_t max_samples, bool overfill)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t                       nsamples = 0;
        auto                         it = queue_.begin();

        for (; it != queue_.end() && nsamples < max_samples; ++it) {
            if (nsamples + (*it)->nsamples < max_samples || overfill)
                nsamples += (*it)->nsamples;
        }

        mpkts.splice(mpkts.end(), queue_, queue_.begin(), it);
        nsamples_ -= nsamples;

        producer_cond_.notify_all();

        return nsamples;
    }

    size_t try_pop(container_type &mpkts)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t                       nsamples = nsamples_;

        mpkts.splice(mpkts.end(), std::move(queue_));
        nsamples_ = 0;

        producer_cond_.notify_all();

        return nsamples;
    }

    size_t pop(container_type &mpkts)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        consumer_cond_.wait(lock, [this]{ return done_ || kicked_ || nsamples_ > 0; });

        if (kicked_) {
            kicked_.store(true, std::memory_order_release);
            return 0;
        }

        size_t nsamples = nsamples_;

        mpkts.splice(mpkts.end(), std::move(queue_));
        nsamples_ = 0;

        producer_cond_.notify_all();

        return nsamples;
    }

    void push(T &&mpkt)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        nsamples_ += mpkt->nsamples;

        queue_.push_back(std::move(mpkt));

        consumer_cond_.notify_one();

        producer_cond_.wait(lock, [this]{ return done_ || kicked_ || !high_water_mark_ || nsamples_ < *high_water_mark_; });

        if (kicked_)
            kicked_.store(false, std::memory_order_release);
    }

    /** @brief Kick the queue to force progress */
    void kick(void)
    {
        kicked_.store(true, std::memory_order_release);

        producer_cond_.notify_all();
        consumer_cond_.notify_all();
    }

protected:
    /** @brief Are we done with the queue? */
    std::atomic<bool> done_;

    /** @brief Is the queue being kicked? */
    std::atomic<bool> kicked_;

    /** @brief Maximum number of IQ samples the queue may contain */
    std::optional<size_t> high_water_mark_;

    /** @brief Number of IQ samples the queue contains */
    size_t nsamples_;

    /** @brief Mutex protecting the queue */
    std::mutex mutex_;

    /** @brief Producer condition variable */
    std::condition_variable producer_cond_;

    /** @brief Consumer condition variable */
    std::condition_variable consumer_cond_;

    /** @brief Queue of modulated packets */
    container_type queue_;
};

#endif /* MODPACKETQUEUE_HH_ */
