// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SYNC_BARRIER_HH
#define SYNC_BARRIER_HH

#include <atomic>
#include <mutex>

#include "barrier.hh"

/** @brief Synchronize access to shared state */
class sync_barrier
{
public:
    sync_barrier(unsigned count)
      : done_(false)
      , barrier_(count)
    {
        synchronize_.store(true, std::memory_order_release);
    }

    sync_barrier() = delete;

    /** @brief Return true if state needs to be synchronized */
    bool needs_sync() const
    {
        return synchronize_.load(std::memory_order_acquire);
    }

    /** @brief Synchronize with state change */
    void sync()
    {
        // Synchronize on start of state change
        barrier_.wait();

        // Synchronize on end of state change
        barrier_.wait();
    }

    /** @brief Synchronization on state modification
     * @tparam Callable type
     * @param f Callable to invoke to modify state.
     */
    template <typename F>
    bool modify(F&& f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If we are done, no further state changes are permitted. Not only are
        // they useless, but they can also lead to deadlock.
        if (done_)
            return false;

        // Use RIIA to handle possible exception in f
        {
            scoped_sync sync(*this);

            // Modify state
            f();
        }

        return true;
    }

    /** @brief Synchronization on state modification
     * @tparam Callable type
     * @tparam Callable type
     * @param f Callable to invoke to modify state.
     * @param p Predicat to test before modifying state.
     */
    template <typename F, typename P>
    bool modify(F&& f, P&& p)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If we are done, no further state changes are permitted. Not only are
        // they useless, but they can also lead to deadlock.
        if (done_)
            return false;

        // Use RIIA to handle possible exception in f
        if (p()) {
            scoped_sync sync(*this);

            // Modify state
            f();
        }

        return true;
    }

    /** @brief Sleep until state change */
    void sleep_until_state_change()
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.wait(lock, [this]{ return needs_sync(); });
    }

protected:
    /** @brief Modify state gated by a synchronization barrier */
    /** The state mutex must be held before constructing a scoped_sync */
    class scoped_sync {
    public:
        scoped_sync(sync_barrier &sync)
          : sync_(sync)
        {
            // Signal need for synchronization
            sync_.synchronize_.store(true, std::memory_order_release);

            // Wake all dependent threads
            sync_.wake_dependents();

            // Wait for all dependent threads to be ready for state change
            sync_.barrier_.wait();
        }

        scoped_sync() = delete;

        ~scoped_sync()
        {
            // We are done with the state change
            sync_.synchronize_.store(false, std::memory_order_release);

            // Wait for all dependent threads to resume
            sync_.barrier_.wait();
        }

    private:
        /** @brief Synchronization barrier */
        sync_barrier &sync_;
    };

    /** @brief Mutex for shared state. */
    mutable std::mutex mutex_;

    /** @brief Done flag. */
    bool done_;

    /** @brief Mutex for waking threads */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking threads */
    std::condition_variable wake_cond_;

    /** @brief Wake all threads dependent on synchronized values */
    virtual void wake_dependents()
    {
        // Wake all threads that are sleeping until state changes
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }

private:
    /** @brief Synchronization flag. */
    std::atomic<bool> synchronize_;

    /** @brief Barrier for thread synchronization */
    barrier barrier_;
};

#endif /* SYNC_BARRIER_HH */
