#ifndef QUEUE_HH_
#define QUEUE_HH_

#include <functional>

#include "SafeQueue.hh"
#include "net/Element.hh"

using namespace std::placeholders;

/** @brief A (FIFO) queue Element that has a separate FIFO high-priority queue
 * that is always serviced first.
 */
template <class T>
class Queue : public Element {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    Queue()
      : in(*this,
           nullptr,
           nullptr,
           std::bind(&Queue<T>::push, this, _1))
      , out(*this,
            std::bind(&Queue<T>::reset, this),
            std::bind(&Queue<T>::stop, this),
            std::bind(&Queue<T>::pop, this, _1))
    {
    }

    virtual ~Queue()
    {
        stop();
    }

    /** @brief Reset queue to empty state. */
    virtual void reset(void)
    {
        std::lock_guard<std::mutex> lock(m_);
        std::list<T> newq;

        done_ = false;
        q_.swap(newq);
    }

    /** @brief Push an element on the end of the queue. */
    virtual void push(T&& val)
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            q_.push_back(std::move(val));
        }

        cond_.notify_one();
    }

    /** @brief Push an element on the end of the high-priority queue. */
    virtual void push_hi(T&& item)
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.push_back(item);
        }

        cond_.notify_one();
    }

    virtual void splice_hi(std::list<T>& items)
    {
        std::unique_lock<std::mutex> lock(m_);

        hiq_.splice(hiq_.end(), items);

        cond_.notify_all();
    }

    virtual void splice_hi(std::list<T>& items, const_iterator first, const_iterator last)
    {
        std::unique_lock<std::mutex> lock(m_);

        hiq_.splice(hiq_.end(), items, first, last);

        cond_.notify_all();
    }

    virtual bool pop(T& val)
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || !hiq_.empty() || !q_.empty(); });
        if (!hiq_.empty()) {
            val = std::move(hiq_.front());
            hiq_.pop_front();
            return true;
        } else if (!q_.empty()) {
            val = std::move(q_.front());
            q_.pop_front();
            return true;
        } else
            return false;
    }

    virtual void stop(void)
    {
        done_ = true;
        cond_.notify_all();
    }

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;

protected:
    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Mutex protecting the queues. */
    std::mutex m_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief The high-priority queue itself. */
    std::list<T> hiq_;

    /** @brief The standard_priority queue itself. */
    std::list<T> q_;
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

using RadioQueue = Queue<std::shared_ptr<RadioPacket>>;

#endif /* QUEUE_HH_ */
