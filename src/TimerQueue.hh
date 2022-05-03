// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <chrono>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "heap.hh"
#include "Clock.hh"

class TimerQueue
{
public:
    using time_type = MonoClock::time_point;

    struct Timer : public heap<Timer>::element
    {
        Timer() = default;
        virtual ~Timer() = default;

        /** @brief Timer action */
        virtual void operator()() = 0;

        /** @brief Compare timers according to deadline */
        bool operator <(const Timer &other) const
        {
            return deadline < other.deadline;
        }

        /** @brief Timer deadline */
        time_type deadline;
    };

    TimerQueue() : done_(true)
    {
    }

    ~TimerQueue()
    {
        stop();
    }

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;

    TimerQueue& operator=(const TimerQueue&) = delete;
    TimerQueue& operator=(TimerQueue&&) = delete;

    /** @brief Run a timer after a delta */
    void run_in(Timer &t, const time_type::duration &delta)
    {
        run_at(t, MonoClock::now() + delta);
    }

    /** @brief Run a timer at a specific time */
    void run_at(Timer &t, const time_type &when);

    /** @brief Return true if a timer is running */
    bool running(const Timer &t)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return t.in_heap();
    }

    /** @brief Cancel a timer */
    void cancel(Timer &t)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (t.in_heap())
            timer_queue_.remove(t);
    }

    /** @brief Update a timer whose deadline has changed */
    void update(Timer &t);

    /** @brief Execute timer events. */
    void run(void);

    /** @brief Start a thread to process timers. */
    void start(void);

    /** @brief Stop the thread processing timers. */
    void stop(void);

private:
    /** @brief Mutex protecting event queue. */
    std::mutex mutex_;

    /** @brief Event queue. */
    heap<Timer> timer_queue_;

    /** @brief Flag indicating we are done processing timers. */
    bool done_;

    /** @brief Thread that runs the timer worker. */
    std::thread timer_worker_thread_;

    /** @brief Timer worker. */
    void timer_worker(void);
};

template<class T>
class TimerCallback : public TimerQueue::Timer
{
public:
    TimerCallback(T &&callback)
      : callback_(callback)
    {
    }

    TimerCallback() = delete;

    void operator()() override final
    {
        callback_();
    }

protected:
    /** @brief Timer callback */
    T callback_;
};
