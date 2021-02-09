// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_HH_
#define UTIL_HH_

#include <signal.h>
#include <unistd.h>

#include <sys/capability.h>

#include <thread>

/** @brief sprintf to a std::string. */
std::string sprintf(const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 1, 2)))
#endif
;

/** @brief Exec a command. */
int exec(const std::vector<std::string>&);

class Caps {
public:
    Caps() : caps_(cap_init())
    {
        if (caps_ == NULL)
            throw std::runtime_error(strerror(errno));
    }

    Caps(const Caps& other) : caps_(cap_dup(other.caps_))
    {
        if (caps_ == NULL)
            throw std::runtime_error(strerror(errno));
    }

    Caps(Caps&& other) noexcept : caps_(other.caps_)
    {
        other.caps_ = NULL;
    }

    Caps(cap_t caps) : caps_(caps)
    {
    }

    ~Caps()
    {
        if (caps_ != NULL)
            cap_free(caps_);
    }

    Caps& operator=(const Caps& other)
    {
        if ((caps_ = cap_dup(other.caps_)) == NULL)
            throw std::runtime_error(strerror(errno));

        return *this;
    }

    Caps& operator=(Caps&& other) noexcept
    {
        caps_ = other.caps_;
        other.caps_ = NULL;

        return *this;
    }

    Caps& operator=(cap_t caps) noexcept
    {
        if (caps_ != NULL)
            cap_free(caps_);

        caps_ = caps;

        return *this;
    }

    void set_proc()
    {
        if (cap_set_proc(caps_) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear()
    {
        if (cap_clear(caps_) != 0)
            throw std::runtime_error(strerror(errno));
    }

    cap_flag_value_t get_flag(cap_value_t cap, cap_flag_t flag)
    {
        cap_flag_value_t value;

        if (cap_get_flag(caps_, cap, flag, &value) != 0)
            throw std::runtime_error(strerror(errno));

        return value;
    }

    void set_flag(cap_flag_t flag, const std::vector<cap_value_t>& caps)
    {
        if (cap_set_flag(caps_, flag, caps.size(), &caps[0], CAP_SET) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear_flag(cap_flag_t flag)
    {
        if (cap_clear_flag(caps_, flag) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear_flag(cap_flag_t flag, const std::vector<cap_value_t>& caps)
    {
        if (cap_set_flag(caps_, flag, caps.size(), &caps[0], CAP_CLEAR) != 0)
            throw std::runtime_error(strerror(errno));
    }

protected:
    cap_t caps_;
};

class RaiseCaps
{
public:
    RaiseCaps(const std::vector<cap_value_t>& caps)
      : orig_caps_(cap_get_proc())
    {
        Caps new_caps(cap_get_proc());

        new_caps.set_flag(CAP_EFFECTIVE, caps);
        new_caps.set_proc();
    }

    RaiseCaps() = delete;
    RaiseCaps(const RaiseCaps&) = delete;
    RaiseCaps(RaiseCaps&&) = delete;

    ~RaiseCaps()
    {
        orig_caps_.set_proc();
    }

    RaiseCaps& operator=(const RaiseCaps&) = delete;
    RaiseCaps& operator=(RaiseCaps&& ) = delete;

protected:
    Caps orig_caps_;
};

/** @brief Make thread have real-time priority. */
void setRealtimePriority(void);

/** @brief Make current thread high-priority. */
void makeThisThreadHighPriority(void);

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

#endif /* UTIL_HH_ */
