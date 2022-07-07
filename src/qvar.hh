// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef QVAR_HH
#define QVAR_HH

#include <condition_variable>
#include <mutex>

template <class T>
class qvar {
public:
    qvar()
      : enabled_(true)
      , full_(false)
    {
    }

    ~qvar()
    {
        disable();
    }

    qvar(const qvar&) = delete;
    qvar(qvar&&) = delete;

    qvar& operator=(const qvar&) = delete;
    qvar& operator=(qvar&&) = delete;

    /** @brief Is the queue enabled? */
    bool isEnabled(void) const
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            return enabled_;
        }
    }

    /** @brief Enable the queue. */
    void enable(void)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            enabled_ = true;
        }

        cv_.notify_all();
    }

    /** @brief Disable the queue. */
    void disable(void)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            enabled_ = false;
        }

        cv_.notify_all();
    }

    /** @brief Set qvar contents */
    qvar& operator =(const T &val)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            val_ = val;
            full_ = true;
        }

        cv_.notify_all();

        return *this;
    }

    /** @brief Set qvar contents */
    qvar& operator =(T &&val)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);

            val_ = std::move(val);
            full_ = true;
        }

        cv_.notify_all();

        return *this;
    }

    bool pop(T &val)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this]{ return !enabled_ || full_; });

        if (!enabled_ || !full_)
            return false;
        else {
            val = std::move(val_);
            full_ = false;
            return true;
        }
    }

protected:
    /** @brief Lock for value  */
    mutable std::mutex mutex_;

    /** @brief Flag indicating that the queue is enabled. */
    bool enabled_;

    /** @brief Condition variable for value */
    std::condition_variable cv_;

    /** @brief The value */
    T val_;

    /** @brief Full */
    bool full_;
};

#endif /* QVAR_HH */
