#ifndef SIZEDQUEUE_HH_
#define SIZEDQUEUE_HH_

#include <list>
#include <random>

#include "logging.hh"
#include "Clock.hh"
#include "llc/Controller.hh"
#include "net/Queue.hh"

/** @brief A queue that tracks its size. */
template <class T>
class SizedQueue : public Queue<T>, public ControllerNetLink {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    using ControllerNetLink::canPop;

    SizedQueue()
      : Queue<T>()
      , enabled_(true)
      , size_(0)
    {
    }

    virtual ~SizedQueue()
    {
        disable();
    }

    /** @brief Get flag indicating whether or not to be gentle */
    std::set<FlowUID> getHiPriorityFlows(void) const
    {
        std::lock_guard<std::mutex> lock(m_);

        return hi_priority_flows_;
    }

    /** @brief Set flag indicating whether or not to be gentle */
    void setHiPriorityFlows(const std::set<FlowUID> &flows)
    {
        std::lock_guard<std::mutex> lock(m_);

        hi_priority_flows_ = flows;
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

        return size_;
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);

        enabled_ = true;
        size_ = 0;
        hiq_.clear();
        q_.clear();
    }

    virtual void push_hi(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            size_ += item->payload_size;

            hiq_.emplace_front(std::move(item));
        }

        cond_.notify_one();
    }

    virtual void repush(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            size_ += item->payload_size;

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
        if (pop_queue(hiq_, now, val))
            return true;

        // Then look in the default queue
        return pop_queue(q_, now, val);
    }

protected:
    /** @brief Mutex protecting the queues. */
    mutable std::mutex m_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief High-priority flows. */
    std::set<FlowUID> hi_priority_flows_;

    /** @brief Size of queue (bytes). */
    size_t size_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief The high-priority queue. */
    std::list<T> hiq_;

    /** @brief The standard_priority queue. */
    std::list<T> q_;

    /** @brief Attempt to pop packet from given queue. */
    bool pop_queue(std::list<T>& q, const MonoClock::time_point &now, T& val)
    {
        auto it = q.begin();

        while (it != q.end()) {
            if ((*it)->shouldDrop(now)) {
                size_ -= (*it)->payload_size;
                drop(*it);

                it = q.erase(it);
            } else if (canPop(*it)) {
                size_ -= (*it)->payload_size;
                val = std::move(*it);

                q.erase(it);
                return true;
            } else
                it++;
        }

        return false;
    }

    /** @brief Indicate that a packet has been dropped. */
    void drop(const std::shared_ptr<NetPacket> &pkt) const
    {
        if (logger)
            logger->logQueueDrop(MonoClock::now(), pkt);
    }
};

using SizedNetQueue = SizedQueue<std::shared_ptr<NetPacket>>;

#endif /* SIZEDQUEUE_HH_ */
