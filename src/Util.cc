// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>

#include <atomic>
#include <thread>

#include <uhd/utils/thread_priority.hpp>

#include "Logger.hh"
#include "Util.hh"

int sys(const char *fmt, ...)
{
    va_list ap;
    char    cmd[80];
    int     res;

    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    res = system(cmd);

    logEvent("SYSTEM: %s (%d)", cmd, res);

    return res;
}

void setRealtimePriority(pthread_t t)
{
    int                ret;
    constexpr int      policy = SCHED_RR;
    struct sched_param params;

    ret = sched_get_priority_max(policy);
    if (ret == -1) {
        logEvent("SCHEDULER: sched_get_priority_max: %s; error=%d",
            strerror(errno),
            errno);
        return;
    }

    params.sched_priority = ret;

    ret = pthread_setschedparam(t, policy, &params);
    if (ret != 0)
        logEvent("SCHEDULER: pthread_setschedparam: %s; error=%d",
            strerror(ret),
            ret);
}

void makeThisThreadHighPriority(void)
{
    uhd::set_thread_priority_safe();
}

void pinThreadToCPU(pthread_t t, int cpu_num)
{
    int       ret;
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);
    CPU_SET(cpu_num, &cpuset);

    ret = pthread_setaffinity_np(t, sizeof(cpu_set_t), &cpuset);
    if (ret != 0)
        logEvent("SCHEDULER: pthread_setaffinity_np: %s; error=%d",
            strerror(ret),
            ret);
}

void pinThisThread(void)
{
    static std::atomic<unsigned> npinned = 0;
    unsigned                     num_cpus = std::thread::hardware_concurrency();

    pinThreadToCPU(pthread_self(), npinned++ % num_cpus);
}

int doze(double sec)
{
    struct timespec ts;
    double whole, frac;

    frac = modf(sec, &whole);

    ts.tv_sec = whole;
    ts.tv_nsec = frac*1e9;

    return nanosleep(&ts, NULL);
}

BlockSignal::BlockSignal(int sig)
{
    sigset_t block_mask_;

    // Block sig, saving the current signal mask to orig_mask_
    sigemptyset(&block_mask_);
    sigaddset(&block_mask_, sig);

    if (sigprocmask(SIG_BLOCK, &block_mask_, &orig_mask_) == -1) {
        perror("sigprocmask failed");
        exit(1);
    }
}

BlockSignal::~BlockSignal()
{
    // Restore signal mask saved in orig_mask_
    if (sigprocmask(SIG_SETMASK, &orig_mask_, NULL) == -1) {
        perror("sigprocmask failed");
        exit(1);
    }
}

void BlockSignal::unblockAndPause(void)
{
    if (sigsuspend(&orig_mask_) == -1 && errno != EINTR) {
        perror("sigsuspend failed");
        exit(1);
    }
}

static void dummySignalHandler(int)
{
}

void makeThreadWakeable(void)
{
    struct sigaction sa = {{0}};

    sa.sa_handler = dummySignalHandler;

    if (sigaction(SIGWAKE, &sa, NULL) == -1) {
        perror("makeThreadWakeable() failed");
        exit(1);
    }
}

void wakeThread(std::thread& t)
{
    pthread_kill(t.native_handle(), SIGWAKE);
}
