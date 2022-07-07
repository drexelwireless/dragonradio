// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SIMPLEQUEUE_HH_
#define SIMPLEQUEUE_HH_

#include <condition_variable>
#include <mutex>

#include "net/Queue.hh"

/** @brief A simple queue Element. */
template <class T>
class SimpleQueue : public Queue<T>, public ControllerNetLink {
public:
    using ControllerNetLink::canPop;

    using const_iterator = typename std::list<T>::const_iterator;

    enum QueueType {
        FIFO = 0,
        LIFO
    };

    const FlowUID INTERNAL_PORT = 4096;

    SimpleQueue() = delete;

    SimpleQueue(QueueType type)
      : Queue<T>()
      , enabled_(true)
      , type_(type)
    {
    }

    virtual ~SimpleQueue()
    {
        disable();
    }

    /** @brief Get queue type */
    QueueType getQueueType(void) const
    {
        std::lock_guard<std::mutex> lock(m_);

        return type_;
    }

    /** @brief Set queue type */
    void setQueueType(QueueType type)
    {
        std::lock_guard<std::mutex> lock(m_);

        type_ = type;
    }

    bool isEnabled(void) const override
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            return enabled_;
        }
    }

    void enable(void) override
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            enabled_ = true;
        }

        cond_.notify_all();
    }

    void disable(void) override
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            enabled_ = false;
        }

        cond_.notify_all();
    }

    virtual size_t size(void) override
    {
        std::unique_lock<std::mutex> lock(m_);

        return hiq_.size() + q_.size();
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);
        std::list<T> newq;

        enabled_ = true;
        q_.swap(newq);
    }

    virtual void push(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            if (item->flow_uid && *item->flow_uid == INTERNAL_PORT)
                hiq_.emplace_back(std::move(item));
            else
                q_.emplace_back(std::move(item));
        }

        cond_.notify_one();
    }

    virtual void push_hi(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.emplace_front(std::move(item));
        }

        cond_.notify_one();
    }

    virtual void repush(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            if (item->hdr.flags.syn)
                hiq_.emplace_front(std::move(item));
            else
                hiq_.emplace_back(std::move(item));
        }

        cond_.notify_one();
    }

    virtual bool pop(T& val) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [&]{ return !enabled_ || !hiq_.empty() || !q_.empty(); });

        if (!enabled_)
            return false;

        MonoClock::time_point now = MonoClock::now();

        // First look in high-priority queue
        {
            auto it = hiq_.begin();

            while (it != hiq_.end()) {
                if ((*it)->shouldDrop(now))
                    it = hiq_.erase(it);
                else if (canPop(*it)) {
                    val = std::move(*it);
                    hiq_.erase(it);
                    return true;
                } else
                    it++;
            }
        }

        // Then look in the network queue
        if (type_ == FIFO) {
            auto it = q_.begin();

            while (it != q_.end()) {
                if ((*it)->shouldDrop(now))
                    it = q_.erase(it);
                else if (canPop(*it)) {
                    val = std::move(*it);
                    q_.erase(it);
                    return true;
                } else
                    it++;
            }
        } else /* type_ == LIFO */ {
            auto it = q_.rbegin();

            while (it != q_.rend()) {
                if ((*it)->shouldDrop(now)) {
                    it = decltype(it){ q_.erase(std::next(it).base()) };
                } else if (canPop(*it)) {
                    val = std::move(*it);
                    q_.erase(std::next(it).base());
                    return true;
                } else
                    it++;
            }
        }

        return false;
    }

protected:
    /** @brief Mutex protecting the queues. */
    mutable std::mutex m_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief Queue type. */
    QueueType type_;

    /** @brief The high-priority queue itself. */
    std::list<T> hiq_;

    /** @brief The standard_priority queue itself. */
    std::list<T> q_;
};

using SimpleNetQueue = SimpleQueue<std::shared_ptr<NetPacket>>;

#endif /* SIMPLEQUEUE_HH_ */
