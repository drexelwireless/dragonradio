#ifndef QUEUE_HH_
#define QUEUE_HH_

#include <functional>

#include "spinlock_mutex.hh"
#include "Header.hh"
#include "SafeQueue.hh"
#include "net/Element.hh"

using namespace std::placeholders;

/** @brief A queue Element that has a separate high-priority queue that is
 *always serviced first.
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

    virtual ~Queue() = default;

    /** @brief Reset queue to empty state. */
    virtual void reset(void) = 0;

    /** @brief Push an element onto the queue. */
    virtual void push(T&& val) = 0;

    /** @brief Push an element onto the front of the high-priority queue. */
    virtual void push_hi_front(T&& item) = 0;

    /** @brief Push an element onto the back of the high-priority queue. */
    virtual void push_hi_back(T&& item) = 0;

    /** @brief Splice a list of elements onto the the high-priority queue. */
    virtual void splice_hi(std::list<T>& items) = 0;

    /** @brief Splice a list of elements onto the the high-priority queue. */
    virtual void splice_hi(std::list<T>& items, const_iterator first, const_iterator last) = 0;

    /** @brief Pop an element from the queue. */
    virtual bool pop(T& val) = 0;

    /** @brief Stop processing queue elements. */
    virtual void stop(void) = 0;

    /** @brief Set whether or not a node's send window is open */
    void setSendWindowStatus(NodeId id, bool isOpen)
    {
        std::lock_guard<spinlock_mutex> lock(send_window_status_mutex_);

        send_window_status_[id] = isOpen;
    }

    /** @brief The queue's packet input port. */
    Port<In, Push, T> in;

    /** @brief The queue's packet output port. */
    Port<Out, Pull, T> out;

protected:
    /** @brief Mutex protecting the send window status */
    spinlock_mutex send_window_status_mutex_;

    /** @brief Nodes' send window statuses */
    std::unordered_map<NodeId, bool> send_window_status_;

    /** @brief Return true if the packet can be popped */
    bool canPop(const T& pkt)
    {
        if (pkt->isFlagSet(kBroadcast))
            return true;

        std::lock_guard<spinlock_mutex> lock(send_window_status_mutex_);
        auto                            it = send_window_status_.find(pkt->nexthop);

        if (it != send_window_status_.end())
            return it->second;
        else
            return true;
    }
};

using NetQueue = Queue<std::shared_ptr<NetPacket>>;

using RadioQueue = Queue<std::shared_ptr<RadioPacket>>;

/** @brief A simple queue Element. */
template <class T>
class SimpleQueue : public Queue<T> {
public:
    using const_iterator = typename std::list<T>::const_iterator;

    SimpleQueue() : Queue<T>()
    {
    }

    virtual ~SimpleQueue()
    {
        stop();
    }

    virtual void reset(void) override
    {
        std::lock_guard<std::mutex> lock(m_);
        std::list<T> newq;

        done_ = false;
        q_.swap(newq);
    }

    virtual void push(T&& val) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            q_.push_back(std::move(val));
        }

        cond_.notify_one();
    }

    virtual void push_hi_front(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.push_front(item);
        }

        cond_.notify_one();
    }

    virtual void push_hi_back(T&& item) override
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            hiq_.push_back(item);
        }

        cond_.notify_one();
    }

    virtual void splice_hi(std::list<T>& items) override
    {
        std::unique_lock<std::mutex> lock(m_);

        hiq_.splice(hiq_.end(), items);

        cond_.notify_all();
    }

    virtual void splice_hi(std::list<T>& items, const_iterator first, const_iterator last) override
    {
        std::unique_lock<std::mutex> lock(m_);

        hiq_.splice(hiq_.end(), items, first, last);

        cond_.notify_all();
    }

    virtual void stop(void) override
    {
        done_ = true;
        cond_.notify_all();
    }

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

/** @brief A FIFO queue Element. */
template <class T>
class FIFO : public SimpleQueue<T> {
public:
    using SimpleQueue<T>::canPop;
    using SimpleQueue<T>::done_;
    using SimpleQueue<T>::m_;
    using SimpleQueue<T>::cond_;
    using SimpleQueue<T>::hiq_;
    using SimpleQueue<T>::q_;

    FIFO() = default;

    virtual ~FIFO() = default;

    virtual bool pop(T& val) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || !hiq_.empty() || !q_.empty(); });

        // If we're done, we're done
        if (done_)
            return false;

        Clock::time_point now = Clock::now();

        // First look in high-priority queue
        if (!hiq_.empty()) {
            val = std::move(hiq_.front());
            hiq_.pop_front();
            return true;
        }

        // Then look in the network queue, FIFO-style
        {
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
        }

        return false;
    }
};

using NetFIFO = FIFO<std::shared_ptr<NetPacket>>;

using RadioFIFO = FIFO<std::shared_ptr<RadioPacket>>;

/** @brief A LIFO queue Element. */
template <class T>
class LIFO : public SimpleQueue<T> {
public:
    using SimpleQueue<T>::canPop;
    using SimpleQueue<T>::done_;
    using SimpleQueue<T>::m_;
    using SimpleQueue<T>::cond_;
    using SimpleQueue<T>::hiq_;
    using SimpleQueue<T>::q_;

    LIFO() = default;

    virtual ~LIFO() = default;

    virtual bool pop(T& val) override
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return done_ || !hiq_.empty() || !q_.empty(); });

        // If we're done, we're done
        if (done_)
            return false;

        Clock::time_point now = Clock::now();

        // First look in high-priority queue
        if (!hiq_.empty()) {
            val = std::move(hiq_.front());
            hiq_.pop_front();
            return true;
        }

        // Then look in the network queue, LIFO-style
        {
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
};

using NetLIFO = LIFO<std::shared_ptr<NetPacket>>;

using RadioLIFO = LIFO<std::shared_ptr<RadioPacket>>;

#endif /* QUEUE_HH_ */
