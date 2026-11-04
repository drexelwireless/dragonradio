#ifndef TAILDROPQUEUE_HH_
#define TAILDROPQUEUE_HH_

#include <list>

#include "Clock.hh"
#include "net/Queue.hh"
#include "net/SizedQueue.hh"

/** @brief A tail dropD queue. */
template <class T>
class TailDropQueue : public SizedQueue<T> {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    using SizedQueue<T>::canPop;
    using SizedQueue<T>::drop;
    using SizedQueue<T>::size_;
    using SizedQueue<T>::hi_priority_flows_;
    using SizedQueue<T>::m_;
    using SizedQueue<T>::cond_;
    using SizedQueue<T>::hiq_;
    using SizedQueue<T>::q_;

    TailDropQueue(size_t max_size)
      : SizedQueue<T>()
      , max_size_(max_size)
    {
    }

    TailDropQueue() = delete;

    /** @brief Get maximum size */
    size_t getMaxSize(void) const
    {
        return max_size_;
    }

    /** @brief Set maximum size */
    void setMaxSize(size_t max_size)
    {
        max_size_ = max_size;
    }

    virtual void push(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            bool                        mark = false;

            if (size_ > max_size_)
              mark = true;

            // A full queue must not let packets to a node whose send window
            // is closed (unreachable or slow to ACK) starve every other
            // destination: an arrival that may leave pushes out the oldest
            // queued packets that may not (P5 finding; affects hardware runs
            // with --taildrop). With one destination, or every window open,
            // nothing changes.
            if (mark && canPop(item)) {
                for (auto it = q_.begin(); it != q_.end() && size_ > max_size_; ) {
                    if (!canPop(*it)) {
                        size_ -= (*it)->payload_size;
                        drop(*it);
                        it = q_.erase(it);
                    } else
                        ++it;
                }
                mark = size_ > max_size_;
            }

            if (mark)
                drop(item);

            if (!mark) {
                size_ += item->payload_size;

                if (item->flow_uid && hi_priority_flows_.find(*item->flow_uid) != hi_priority_flows_.end())
                    hiq_.emplace_back(std::move(item));
                else
                    q_.emplace_back(std::move(item));
            }
        }

        cond_.notify_one();
    }

protected:
    /** @brief Maximum size. */
    size_t max_size_;
};

using TailDropNetQueue = TailDropQueue<std::shared_ptr<NetPacket>>;

#endif /* TAILDROPQUEUE_HH_ */
