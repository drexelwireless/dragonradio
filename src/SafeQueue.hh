// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SAFEQUEUE_H_
#define SAFEQUEUE_H_

#include <condition_variable>
#include <deque>
#include <mutex>

/** @brief A thread-safe queue. */
/** A SafeQueue is a thread-safe FIFO queue. Any call to pop will block until an
 * element is inserted until the queue is stopped by a call to stop. Once stop
 * has been invoked, elements can still be inserted, but any call to pop will
 * immediately return.
 */
template<typename T, class Container = std::deque<T>>
class SafeQueue {
public:
    using container_type = Container;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    SafeQueue()
      : enabled_(true)
    {
    }

    ~SafeQueue()
    {
        disable();
    }

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue(SafeQueue&&) = delete;

    SafeQueue& operator=(const SafeQueue&) = delete;
    SafeQueue& operator=(SafeQueue&&) = delete;

    /** @brief Is the queue enabled? */
    bool isEnabled(void) const
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            return enabled_;
        }
    }

    /** @brief Enable the queue. */
    void enable(void)
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            enabled_ = true;
        }

        cond_.notify_all();
    }

    /** @brief Disable the queue. */
    void disable(void)
    {
        {
            std::unique_lock<std::mutex> lock(m_);

            enabled_ = false;
        }

        cond_.notify_all();
    }

    /** @brief Get queue size. */
    size_t size(void)
    {
        std::lock_guard<std::mutex> lock(m_);

        return q_.size();
    }

    /** @brief Clear queue contents. */
    void clear(void)
    {
        std::lock_guard<std::mutex> lock(m_);

        return q_.clear();
    }

    /** @brief Return true if the queue is empty. */
    bool empty(void) const
    {
        std::lock_guard<std::mutex> lock(m_);

        return q_.empty();
    }

    /** @brief Push an element on the end of the queue. */
    void push(const T& val)
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            q_.push_back(val);
        }

        cond_.notify_one();
    }

    /** @brief Push an element on the end of the queue. */
    void push(T&& val)
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            q_.push_back(std::move(val));
        }

        cond_.notify_one();
    }

    /** @brief Construct an element in-place on the end of the queue. */
    template<class... Args>
    void emplace(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(m_);

            q_.emplace_back(std::forward<Args>(args)...);
        }

        cond_.notify_one();
    }

    /** @brief Access the first element of the queue and pop it.
     * @param val Reference to location where popped value should be copied.
     * @return true if a value was popped, false otherwise.
     */
    bool pop(T& val)
    {
        std::unique_lock<std::mutex> lock(m_);

        cond_.wait(lock, [this]{ return !enabled_ || !q_.empty(); });

        if (!enabled_ || q_.empty())
            return false;
        else {
            val = std::move(q_.front());
            q_.pop_front();
            return true;
        }
    }

    /** @brief Access the first element of the queue and pop it without waiting.
     * @param val Reference to location where popped value should be copied.
     * @return true if a value was popped, false otherwise.
     */
    bool try_pop(T& val)
    {
        std::unique_lock<std::mutex> lock(m_);

        if (!enabled_ || q_.empty())
            return false;
        else {
            val = std::move(q_.front());
            q_.pop_front();
            return true;
        }
    }

private:
    /** @brief Mutex protecting the queue. */
    mutable std::mutex m_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief The queue itself. */
    container_type q_;
};

#endif /* SAFEQUEUE_H_ */
