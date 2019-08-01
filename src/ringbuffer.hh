#ifndef _RINGBUFFER_HH_
#define _RINGBUFFER_HH_

#include <atomic>

template <class T, unsigned LOGN>
class ringbuffer
{
public:
    using size_type = unsigned;

    ringbuffer()
    {
        ridx_.store(0, std::memory_order_release);
        widx_.store(0, std::memory_order_release);
    }

    void clear()
    {
        ridx_.store(0, std::memory_order_release);
        widx_.store(0, std::memory_order_release);
    }

    constexpr size_type capacity() const
    {
        return N;
    }

    size_type size() const
    {
        return widx_.load(std::memory_order_acquire) -
            ridx_.load(std::memory_order_acquire);
    }

    T& front()
    {
        return items_[ridx_.load(std::memory_order_acquire) & MASK];
    }

    const T& front() const
    {
        return items_[ridx_.load(std::memory_order_acquire) & MASK];
    }

    void pop()
    {
        ridx_.fetch_add(1, std::memory_order_release);
    }

    bool push(const T& val)
    {
        if (size() == capacity())
            return false;

        items_[widx_.load(std::memory_order_acquire) & MASK] = val;
        widx_.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool push(T&& val)
    {
        if (size() == capacity())
            return false;

        items_[widx_.load(std::memory_order_acquire) & MASK] = std::move(val);
        widx_.fetch_add(1, std::memory_order_release);
        return true;
    }

protected:
    static constexpr unsigned N = 1 << LOGN;
    static constexpr unsigned MASK = N - 1;

    /** @brief Items in the ring buffer */
    T items_[N];

    /** @brief The read index */
    std::atomic<unsigned> ridx_;

    /** @brief The write index */
    std::atomic<unsigned> widx_;
};

#endif /* _RINGBUFFER_HH_ */
