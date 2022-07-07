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
      : enabled_(true)
      , nsamples_(0)
    {
    }

    ~ModPacketQueue()
    {
        disable();
    }

    std::optional<size_t> getHighWaterMark(void) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return high_water_mark_;
    }

    void setHighWaterMark(std::optional<size_t> high_water_mark)
    {
        std::lock_guard<std::mutex> lock(mutex_);

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

        producer_cond_.notify_all();
        consumer_cond_.notify_all();
    }

    /** @brief Disable the queue. */
    void disable(void)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            enabled_ = false;
        }

        producer_cond_.notify_all();
        consumer_cond_.notify_all();
    }

    size_t try_pop(container_type &mpkts, size_t max_samples, bool overfill)
    {
        size_t nsamples = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto                         it = queue_.begin();

            for (; it != queue_.end() && nsamples < max_samples; ++it) {
                if (nsamples + (*it)->nsamples < max_samples || overfill)
                    nsamples += (*it)->nsamples;
            }

            mpkts.splice(mpkts.end(), queue_, queue_.begin(), it);
            nsamples_ -= nsamples;
        }

        producer_cond_.notify_all();

        return nsamples;
    }

    size_t try_pop(container_type &mpkts)
    {
        size_t nsamples;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            nsamples = nsamples_;
            mpkts.splice(mpkts.end(), std::move(queue_));
            nsamples_ = 0;
        }

        producer_cond_.notify_all();

        return nsamples;
    }

    size_t pop(container_type &mpkts)
    {
        size_t nsamples;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            consumer_cond_.wait(lock, [this]{ return !enabled_ || nsamples_ > 0; });

            if (!enabled_)
                return 0;

            nsamples = nsamples_;
            mpkts.splice(mpkts.end(), std::move(queue_));
            nsamples_ = 0;
        }

        producer_cond_.notify_all();

        return nsamples;
    }

    void push(T &&mpkt)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            mpkt->start = nsamples_;
            nsamples_ += mpkt->nsamples;

            queue_.push_back(std::move(mpkt));
        }

        consumer_cond_.notify_one();
    }

    /** @brief Wait for there to be room to push
     * @return true if there is room in the queue
     */
    bool wait_until_room(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        producer_cond_.wait(lock, [this]{ return !enabled_ || !high_water_mark_ || nsamples_ < *high_water_mark_; });

        return !high_water_mark_ || nsamples_ < *high_water_mark_;
    }

protected:
    /** @brief Mutex protecting the queue */
    mutable std::mutex mutex_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Maximum number of IQ samples the queue may contain */
    std::optional<size_t> high_water_mark_;

    /** @brief Number of IQ samples the queue contains */
    size_t nsamples_;

    /** @brief Producer condition variable */
    std::condition_variable producer_cond_;

    /** @brief Consumer condition variable */
    std::condition_variable consumer_cond_;

    /** @brief Queue of modulated packets */
    container_type queue_;
};

#endif /* MODPACKETQUEUE_HH_ */
