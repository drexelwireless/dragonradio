#include <chrono>
#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "heap.hh"
#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "Util.hh"

class TimerQueue
{
public:
    using time_type = Clock::time_point;

    struct Timer : public heap<Timer>::element
    {
        Clock::time_point deadline;

        virtual void operator()() = 0;

        bool operator <(const Timer& other) const
        {
            return deadline < other.deadline;
        }
    };

    TimerQueue();
    ~TimerQueue();

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;

    TimerQueue& operator=(const TimerQueue&) = delete;
    TimerQueue& operator=(TimerQueue&&) = delete;

    /** @brief Run a timer after a delta */
    void run_in(Timer& t, const double &delta);

    /** @brief Run a timer at a specific time */
    void run_at(Timer& t, const time_type &when);

    /** @brief Return true if a timer is running */
    bool running(const Timer& t);

    /** @brief Cancel a timer */
    void cancel(Timer& t);

    /** @brief Update a timer whose deadline has changed */
    void update(Timer& t);

    /** @brief Execute timer events. */
    void run(void);

    /** @brief Start a thread to process timers. */
    void start(void);

    /** @brief Stop the thread processing timers. */
    void stop(void);

private:
    /** @brief Mutex protecting event queue. */
    spinlock_mutex mutex_;

    /** @brief Event queue. */
    heap<Timer> timer_queue_;

    /** @brief Flag indicating we are done prcoessing timers. */
    bool done_;

    /** @brief Thread that runs the timer worker. */
    std::thread timer_worker_thread_;

    /** @brief Timer worker. */
    void timer_worker(void);
};
