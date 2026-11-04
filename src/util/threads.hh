// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_THREADS_HH_
#define UTIL_THREADS_HH_

#include <signal.h>
#include <unistd.h>

#include <thread>

/** @brief Make thread have real-time priority. */
void setRealtimePriority(void);

/** @brief Make current thread high-priority. */
void makeThisThreadHighPriority(void);

/** @brief Name a thread (visible in /proc/PID/task/TID/comm, ps, top).
 * Linux truncates names to 15 characters. Observability only.
 */
void setThreadName(std::thread &t, const char *name);

/** @brief Name a thread with an index suffix, e.g. "dr-demod" 0 -> "dr-demod0" */
void setThreadName(std::thread &t, const char *name, unsigned index);

/** @brief Pin thread to given CPU */
void pinThreadToCPU(pthread_t t, int cpu_num);

/** @brief Pin this thread to a CPU */
void pinThisThread(void);

/** @brief Sleep for the specified number of seconds. sleep, usleep, and
 * nanosleep were already taken, so this function is named "doze."
 * @param sec The number of seconds to sleep.
 * @returns -1 if interrupted.
 */
int doze(double sec);

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
