#ifndef SIMPLEQUEUE_HH_
#define SIMPLEQUEUE_HH_

#include "net/Queue.hh"

/** @brief A simple queue Element. */
template <class T>
class SimpleQueue : public Queue<T> {
public:
    using Queue<T>::canPop;

    using const_iterator = typename std::list<T>::const_iterator;

    enum QueueType {
        FIFO = 0,
        LIFO
    };

    const FlowUID INTERNAL_PORT = 4096;

    SimpleQueue() = delete;

    SimpleQueue(QueueType type)
      : Queue<T>()
      , done_(false)
      , type_(type)
    {
    }

    virtual ~SimpleQueue()
    {
        stop();
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

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);
        std::list<T> newq;

        done_ = false;
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

        cond_.wait(lock, [this]{ return done_ || !hiq_.empty() || !q_.empty(); });

        // If we're done, we're done
        if (done_)
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

    virtual void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

    virtual void updateMCS(NodeId, const MCS&) override
    {
    }

protected:
    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Mutex protecting the queues. */
    std::mutex m_;

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
