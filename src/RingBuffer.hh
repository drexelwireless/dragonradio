// Copyright 2018-2025 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef RINGBUFFER_H_
#define RINGBUFFER_H_

#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>

/** @brief A thread-safe ring buffer. */
/** A RingBuffer is a thread-safe FIFO fixed-size ring buffer. Any call to pop
 * will block until an element is inserted or until the buffer is disabled.
 * Elements can be pushed to the buffer as long as there is room even if the
 * buffer is disabled. A push operation is always non-blocking.
 */
template<typename T, std::size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    using size_type = std::size_t;

    RingBuffer()
      : enabled_(true)
      , read_(0)
      , write_(0)
    {
    }

    ~RingBuffer()
    {
        disable();
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;

    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /** @brief Is the queue enabled? */
    bool isEnabled() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return enabled_;
    }

    /** @brief Enable the queue. */
    void enable() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            enabled_ = true;
        }

        cv_.notify_all();
    }

    /** @brief Disable the queue. */
    void disable() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            enabled_ = false;
        }

        cv_.notify_all();
    }

    /** @brief Get capacity. */
    constexpr size_t capacity() const noexcept
    {
        return Capacity;
    }

    /** @brief Get size. */
    size_t size() const noexcept
    {
        size_t write = write_.load(std::memory_order_acquire);
        size_t read = read_.load(std::memory_order_acquire);

        return write - read;
    }

    /** @brief Return true if the buffer is empty. */
    bool empty() const noexcept
    {
        return size() == 0;
    }

    /** @brief Return true if the buffer is full. */
    bool full() const noexcept
    {
        return size() >= Capacity;
    }

    /** @brief Push an element on the end of the buffer. */
    bool push(const T& item)
    {
        return emplace(item);
    }

    /** @brief Push an element on the end of the buffer. */
    bool push(T&& item)
    {
        return emplace(std::move(item));
    }

    /** @brief Construct an element in-place on the end of the buffer. */
    template<class... Args>
    bool emplace(Args&&... args)
    {
        size_t write = write_.load(std::memory_order_relaxed);
        size_t read = read_.load(std::memory_order_acquire);

        if ((write - read) >= Capacity)
            return false;

        buffer_[write & MASK].emplace(std::forward<Args>(args)...);

        write_.store(write + 1, std::memory_order_release);

        cv_.notify_one();

        return true;
    }

    /** @brief Access the first element of the buffer and pop it.
     * @param item Reference to location where popped item should be copied.
     * @return true if a value was popped, false otherwise.
     */
    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] {
            return !enabled_ || read_.load(std::memory_order_acquire) != write_.load(std::memory_order_acquire);
        });

        if (!enabled_)
            return false;

        return unsafe_pop(item);
    }

    /** @brief Access the first element of the buffer and pop it without waiting.
     * @param item Reference to location where popped item should be copied.
     * @return true if a value was popped, false otherwise.
     */
    bool try_pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!enabled_)
            return false;

        return unsafe_pop(item);
    }

    /** @brief Reset ring buffer. Only safe when ring buffer is quiescent */
    void reset(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t i = read_; i < write_; ++i)
            buffer_[i & MASK].reset();

        read_.store(0, std::memory_order_release);
        write_.store(0, std::memory_order_release);
    }

private:
    static constexpr size_type MASK = Capacity - 1;

    /** @brief Mutex protecting the buffer. */
    mutable std::mutex mutex_;

    /** @brief Condition variable protecting the buffer. */
    std::condition_variable cv_;

    /** @brief Flag indicating that the buffer is enabled. */
    bool enabled_;

    /** @brief The ring buffer. */
    std::array<std::optional<T>, Capacity> buffer_;

    /** @brief Read index. */
    alignas(64) std::atomic<std::size_t> read_;

    /** @brief Write index. */
    alignas(64) std::atomic<std::size_t> write_;

    bool unsafe_pop(T& item)
    {
        size_type read = read_.load(std::memory_order_relaxed);
        size_type write = write_.load(std::memory_order_acquire);

        if (read == write)
            return false;

        std::optional<T>& slot = buffer_[read & MASK];

        item = std::move(*slot);
        slot.reset();
        read_.store(read + 1, std::memory_order_release);

        return true;
    }
};

#endif /* RINGBUFFER_H_ */
