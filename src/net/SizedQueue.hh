#ifndef SIZEDQUEUE_HH_
#define SIZEDQUEUE_HH_

#include <list>
#include <random>

#include "logging.hh"
#include "Clock.hh"
#include "net/Queue.hh"

/** @brief A queue that tracks its size. */
template <class T>
class SizedQueue : public Queue<T> {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    using Queue<T>::canPop;

    SizedQueue()
      : Queue<T>()
      , done_(false)
      , kicked_(false)
      , size_(0)
    {
    }

    virtual ~SizedQueue()
    {
        stop();
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

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);

        done_ = false;
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

        cond_.wait(lock, [this]{ return done_ || kicked_ ||!hiq_.empty() || !q_.empty(); });

        if (kicked_) {
            kicked_.store(false, std::memory_order_release);
            return false;
        }

        // If we're done, we're done
        if (done_)
            return false;

        MonoClock::time_point now = MonoClock::now();

        // First look in high-priority queue
        if (pop_queue(hiq_, now, val))
            return true;

        // Then look in the default queue
        return pop_queue(q_, now, val);
    }

    virtual void kick(void) override
    {
        kicked_.store(true, std::memory_order_release);
        cond_.notify_all();
    }

    void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

    void updateMCS(NodeId, const MCS*) override
    {
    }

protected:
    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Is the queue being kicked? */
    std::atomic<bool> kicked_;

    /** @brief High-priority flows. */
    std::set<FlowUID> hi_priority_flows_;

    /** @brief Size of queue (bytes). */
    size_t size_;

    /** @brief Mutex protecting the queues. */
    mutable std::mutex m_;

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
                drop(**it);

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
    void drop(const NetPacket &pkt) const
    {
        if (logger)
            logger->logQueueDrop(WallClock::now(),
                                 pkt.nretrans,
                                 pkt.hdr,
                                 pkt.ehdr(),
                                 pkt.mgen_flow_uid.value_or(0),
                                 pkt.mgen_seqno.value_or(0),
                                 pkt.mcsidx,
                                 pkt.size());
    }
};

using SizedNetQueue = SizedQueue<std::shared_ptr<NetPacket>>;

#endif /* SIZEDQUEUE_HH_ */
