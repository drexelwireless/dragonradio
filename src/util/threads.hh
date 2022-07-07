// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_THREADS_HH_
#define UTIL_THREADS_HH_

#include <signal.h>
#include <unistd.h>

#include <cmath>
#include <chrono>
#include <thread>

/** @brief Make thread have real-time priority. */
void setRealtimePriority(void);

/** @brief Make current thread high-priority. */
void makeThisThreadHighPriority(void);

/** @brief Pin thread to given CPU */
void pinThreadToCPU(pthread_t t, int cpu_num);

/** @brief Pin this thread to a CPU */
void pinThisThread(void);

/** @brief Sleep for the specified duration.
 * @param sleep_duration The duration to sleep.
 */
template<class Rep, class Period>
void sleep_for(const std::chrono::duration<Rep, Period>& sleep_duration)
{
    using namespace std::literals::chrono_literals;

    if (sleep_duration > 0.0s) {
        struct timespec ts;
        double whole, frac;

        frac = modf(std::chrono::duration<double>(sleep_duration).count(), &whole);

        ts.tv_sec = whole;
        ts.tv_nsec = frac*1e9;

        nanosleep(&ts, NULL);
    }
}

/** @brief Sleep until the specified time_point
 * @param sleep_time Sleep deadline.
 */
template<class Clock, class Duration>
void sleep_until(const std::chrono::time_point<Clock,Duration>& sleep_time)
{
    sleep_for(sleep_time - Clock::now());
}

/** @brief The signal we use to wake a thread */
const int SIGWAKE = SIGUSR1;

/** @brief Atomically block a signal */
class BlockSignal {
public:
    /** @brief Save current signal mask and block a signal
     * @param sig The signal to block
     */
    explicit BlockSignal(int sig);

    BlockSignal() = delete;

    /** @brief Restore original signal mask */
    ~BlockSignal();

    /** @brief Atomically unblock signal and pause until we receive a signal */
    void unblockAndPause(void);

protected:
    /** @brief Original signal mask before we blocked a signal */
    sigset_t orig_mask_;
};

/** @brief Make the current thread wakeable. */
/** This function installs a signal handler for SIGUSR1 so that the thread can
 * be woken from a blocking sycall via wakeThread.
 */
void makeThreadWakeable(void);

/** @brief Wake the given thread. */
/** This function sends a SIGUSR1 signal to the given thread to wake it from any
 * blocking syscalls.
 */
void wakeThread(std::thread& t);

#endif /* UTIL_THREADS_HH_ */
