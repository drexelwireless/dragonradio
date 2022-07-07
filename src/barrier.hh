// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef BARRIER_HH
#define BARRIER_HH

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class barrier
{
public:
    barrier(unsigned count)
      : count_(count)
      , arrived_(0)
      , phase_(0)
    {
    }

    barrier() = delete;

    ~barrier() = default;

    void wait(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (++arrived_ == count_) {
            arrived_ = 0;
            phase_++;
            cv_.notify_all();
        } else {
            auto phase = phase_;

            cv_.wait(lock, [&]{ return phase_ > phase; });
        }
    }

private:
    /** @brief Number of threads in barrier synchronization group */
    const unsigned count_;

    /** @brief Mutex for barrier state */
    std::mutex mutex_;

    /** @brief Condition variable for barrier */
    std::condition_variable cv_;

    /** @brief Number of threads that have arrived at the barrier */
    unsigned arrived_;

    /** @brief Current barrier phase */
    unsigned phase_;
};

#endif /* BARRIER_HH */
