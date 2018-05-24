#include <unistd.h>

#include <algorithm>

#include "TimerQueue.hh"
#include "Util.hh"

TimerQueue::TimerQueue() : done_(true)
{
}

TimerQueue::~TimerQueue()
{
    stop();
}

void TimerQueue::run_in(Timer &t, const double &delta)
{
    run_at(t, Clock::now() + delta);
}

void TimerQueue::run_at(Timer &t, const time_type &when)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    t.deadline = when;

    if (t.in_heap())
        timer_queue_.update(t);
    else {
        timer_queue_.push(t);

        // Wake the timer worker if it's running and the timer we just inserted
        // is the first timer that needs to be run.
        if (!done_ && t.is_top())
            wakeThread(timer_worker_thread_);
    }
}

bool TimerQueue::running(const Timer& t)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    return t.in_heap();
}

void TimerQueue::cancel(Timer &t)
{
    std::lock_guard<spinlock_mutex> lock(mutex_);

    if (t.in_heap())
        timer_queue_.remove(t);
}

void TimerQueue::run(void)
{
    time_type now = Clock::now();

    std::unique_lock<spinlock_mutex> lock(mutex_);

    while (!timer_queue_.empty() && (timer_queue_.top().deadline < now)) {
        Timer &t = timer_queue_.top();

        timer_queue_.pop();

        lock.unlock();
        t();
        lock.lock();
    }
}

void TimerQueue::start(void)
{
    if (done_) {
        done_ = false;

        timer_worker_thread_ = std::thread(&TimerQueue::timer_worker, this);
    }
}

void TimerQueue::stop(void)
{
    if (!done_) {
        done_ = true;

        if (timer_worker_thread_.joinable()) {
            wakeThread(timer_worker_thread_);
            timer_worker_thread_.join();
        }
    }
}

void TimerQueue::timer_worker(void)
{
    makeThreadWakeable();

    while (!done_) {
        time_type now = Clock::now();

        // Run all pending timers
        std::unique_lock<spinlock_mutex> lock(mutex_);

        while (!timer_queue_.empty() && (timer_queue_.top().deadline < now)) {
            Timer &t = timer_queue_.top();

            timer_queue_.pop();

            lock.unlock();
            t();
            lock.lock();
        }

        // XXX Sleep until our next timer. There is a potential race condition
        // here: someone could insert a timer in between the time we unlock the
        // queue and the time we sleep.
        if (timer_queue_.empty()) {
            lock.unlock();
            pause();
        } else {
            double delta = (timer_queue_.top().deadline - now).get_real_secs();

            lock.unlock();
            doze(delta);
        }
    }
}
