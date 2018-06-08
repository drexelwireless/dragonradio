#ifndef SAFEQUEUE_H_
#define SAFEQUEUE_H_

#include <condition_variable>
#include <list>
#include <mutex>

/** @brief A thread-safe queue. */
/** A SafeQueue is a thread-safe FIFO queue. Any call to pop will block until an
 * element is inserted until the queue is stopped by a call to stop. Once stop
 * has been invoked, elements can still be inserted, but any call to pop will
 * immediately return.
 */
template<typename T>
class SafeQueue {
public:
    using iterator = typename std::list<T>::iterator;
    using const_iterator = typename std::list<T>::const_iterator;

    SafeQueue();
    ~SafeQueue();

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue(SafeQueue&&) = delete;

    SafeQueue& operator=(const SafeQueue&) = delete;
    SafeQueue& operator=(SafeQueue&&) = delete;

    /** @brief Reset queue to empty state. */
    void reset(void);

    /** @brief Return true if the queue is empty. */
    bool empty(void) const;

    /** @brief Push an element on the end of the queue. */
    void push(const T& val);

    /** @brief Push an element on the end of the queue. */
    void push(T&& val);

    /** @brief Construct an element in-place on the end of the queue. */
    void emplace(const T& val);

    /** @brief Construct an element in-place on the end of the queue. */
    void emplace(T&& val);

    /** @brief Push an element on the front of the queue .*/
    void push_front(const T& val);

    /** @brief Push an element on the front of the queue. */
    void push_front(T&& val);

    /** @brief Construct an element in-place on the front of the queue. */
    void emplace_front(const T& val);

    /** @brief Construct an element in-place on the front of the queue. */
    void emplace_front(T&& val);

    /** @brief Access the first element of the queue and pop it.
     * @param val Reference to location where popped value should be copied.
     * @return true if a value was popped, false otherwise.
     */
    bool pop(T& val);

    /** @brief Transfer elements from a list to the front of this queue. */
    void splice_front(std::list<T>& other);
    void splice_front(std::list<T>&& other);
    void splice_front(std::list<T>& other, const_iterator it);
    void splice_front(std::list<T>&& other, const_iterator it);
    void splice_front(std::list<T>& other, const_iterator first, const_iterator last);
    void splice_front(std::list<T>&& other, const_iterator first, const_iterator last);

    /** @brief Mark the queue as stopped. */
    void stop(void);

private:
    /** @brief Flag indicating that processing of the queue should stop. */
    bool done_;

    /** @brief Mutex protecting the queue. */
    std::mutex m_;

    /** @brief Condition variable protecting the queue. */
    std::condition_variable cond_;

    /** @brief The queue itself. */
    std::list<T> q_;
};

template<typename T>
SafeQueue<T>::SafeQueue() : done_(false)
{
}

template<typename T>
SafeQueue<T>::~SafeQueue()
{
    stop();
}

template<typename T>
void SafeQueue<T>::reset(void)
{
    std::lock_guard<std::mutex> lock(m_);
    std::list<T> newq;

    done_ = false;
    q_.swap(newq);
}

template<typename T>
bool SafeQueue<T>::empty(void) const
{
    std::lock_guard<std::mutex> lock(m_);

    return q_.empty();
}

template<typename T>
void SafeQueue<T>::push(const T& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.push_back(val);
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::push(T&& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.push_back(std::move(val));
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::emplace(const T& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_back(val);
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::emplace(T&& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_back(std::move(val));
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::push_front(const T& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.push_front(val);
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::push_front(T&& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.push_front(std::move(val));
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::emplace_front(const T& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_front(val);
    }

    cond_.notify_one();
}

template<typename T>
void SafeQueue<T>::emplace_front(T&& val)
{
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.emplace_front(std::move(val));
    }

    cond_.notify_one();
}

template<typename T>
bool SafeQueue<T>::pop(T& val)
{
    std::unique_lock<std::mutex> lock(m_);

    cond_.wait(lock, [this]{ return done_ || !q_.empty(); });
    if (q_.empty())
        return false;
    else {
        val = std::move(q_.front());
        q_.pop_front();
        return true;
    }
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>& other)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>&& other)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>& other, const_iterator it)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other, it);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>&& other, const_iterator it)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other, it);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>& other, const_iterator first, const_iterator last)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other, first, last);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::splice_front(std::list<T>&& other, const_iterator first, const_iterator last)
{
    std::unique_lock<std::mutex> lock(m_);

    q_.splice(q_.begin(), other, first, last);

    cond_.notify_all();
}

template<typename T>
void SafeQueue<T>::stop(void)
{
    done_ = true;
    cond_.notify_all();
}

#endif /* SAFEQUEUE_H_ */
