// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <algorithm>
#include <chrono>

#include "TimerQueue.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

void TimerQueue::run_at(Timer& t, const time_type& when)
{
    std::lock_guard<std::mutex> lock(mutex_);

    t.deadline = when;

    if (t.in_heap())
        timer_queue_.update(t);
    else
        timer_queue_.push(t);

    // Wake the timer worker if it's running and the timer we just inserted
    // is the first timer that needs to be run.
    if (!done_ && t.is_top())
        wakeThread(timer_worker_thread_);
}

void TimerQueue::run(void)
{
    time_type now = MonoClock::now();

    std::unique_lock<std::mutex> lock(mutex_);

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
        time_type now = MonoClock::now();

        // Run all pending timers
        std::unique_lock<std::mutex> lock(mutex_);

        while (!timer_queue_.empty() && (timer_queue_.top().deadline < now)) {
            Timer &t = timer_queue_.top();

            timer_queue_.pop();

            lock.unlock();
            t();
            lock.lock();
        }

        // Sleep until our either our next timer fires or we are awoken by a
        // signal.
        if (timer_queue_.empty()) {
            BlockSignal block(SIGWAKE);

            lock.unlock();
            block.unblockAndPause();
        } else {
            now = MonoClock::now();

            MonoClock::duration delta = timer_queue_.top().deadline - now;

            lock.unlock();

            if (delta > 0.0s)
                doze(delta);
        }
    }
}
