#ifndef SMARTLIFO_HH_
#define SMARTLIFO_HH_

#include <unordered_map>

#include "net/Queue.hh"

/** @brief A LIFO queue that is more intelligent about handling retransmissions
 * and high-priority packets.
 */
template <class T>
class SmartLIFO : public Queue<T> {
public:
    using Queue<T>::canPop;

    SmartLIFO()
      : Queue<T>()
      , size_(0)
    {
    }

    virtual ~SmartLIFO()
    {
        stop();
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);

        done_ = false;
        txq_.clear();
        rtxq_.clear();
        size_ = 0;
    }

    virtual void push(T&& pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            // Add to the *front* of the transmission queue: these packets are
            // treated LIFO.
            txq_.push_front(std::move(pkt));
            size_++;
        }

        cond_.notify_one();
    }

    virtual void push_hi(T&& pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.push_back(pkt);
            size_++;
        }

        cond_.notify_one();
    }

    virtual void repush(T&& pkt) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            // Add to the *back* of the RE-transmission queue: retransmissions
            // are sent in FIFO order BEFORE new packets
            rtxq_.push_back(std::move(pkt));
            size_++;
        }

        cond_.notify_one();
    }

    virtual void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

    virtual bool pop(T& pkt) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || size_ != 0; });

        // If we're done, we're done
        if (done_)
            return false;

        // First look in high-priority queue
        if (!hiq_.empty()) {
            pkt = std::move(hiq_.front());
            hiq_.pop_front();
            size_--;
            return true;
        }

        // Then look in the retransmission queue. We don't drop these packets:
        // only the SmartController may do that since they already have a
        // sequence number.
        if (popq(std::nullopt, rtxq_, pkt))
            return true;

        // Finally look in the standard queue.
        MonoClock::time_point now = MonoClock::now();

        return popq(now, txq_, pkt);
    }

protected:
    using queue_t = std::list<T>;

    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Mutex protecting the queues. */
    std::mutex m_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief Number of packets in queues. */
    typename queue_t::size_type size_;

    /** @brief The high-priority queue. */
    queue_t hiq_;

    /** @brief Retransmission queue (for retransmitted packets) */
    queue_t rtxq_;

    /** @brief Transmission queue (for new packets) */
    queue_t txq_;

    /** @brief Pop a packet from the front of a queue */
    /** This function skips packets destined for nodes with closed send windows,
     * and it optionally drops packets whose deadline has passed.
     */
    bool popq(const std::optional<MonoClock::time_point> &now,
              queue_t &q,
              T& pkt)
    {
        auto it = q.begin();

        while (it != q.end()) {
            if (now && (*it)->shouldDrop(*now)) {
                it = q.erase(it);
                --size_;
            } else if (canPop(*it)) {
                pkt = std::move(*it);
                q.erase(it);
                --size_;
                return true;
            } else
                it++;
        }

        return false;
    }
};

using NetSmartLIFO = SmartLIFO<std::shared_ptr<NetPacket>>;

using RadioSmartLIFO = SmartLIFO<std::shared_ptr<RadioPacket>>;

#endif /* SMARTLIFO_HH_ */
