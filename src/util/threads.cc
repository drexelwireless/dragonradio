// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <math.h>

#include <atomic>

#include <uhd/version.hpp>
#if UHD_VERSION >= 3110000
#include <uhd/utils/thread.hpp>
#else /* UHD_VERSION < 3110000 */
#include <uhd/utils/thread_priority.hpp>
#endif /* UHD_VERSION < 3110000 */

#include "logging.hh"
#include "util/capabilities.hh"
#include "util/threads.hh"

void setRealtimePriority(pthread_t t)
{
    RaiseCaps          caps({CAP_SYS_NICE});
    int                ret;
    constexpr int      policy = SCHED_RR;
    struct sched_param params;

    ret = sched_get_priority_max(policy);
    if (ret == -1) {
        logScheduler(LOGERROR, "sched_get_priority_max: %s; error=%d",
            strerror(errno),
            errno);
        return;
    }

    params.sched_priority = ret;

    ret = pthread_setschedparam(t, policy, &params);
    if (ret != 0)
        logScheduler(LOGERROR, "pthread_setschedparam: %s; error=%d",
            strerror(ret),
            ret);
}

void makeThisThreadHighPriority(void)
{
    RaiseCaps caps({CAP_SYS_NICE});

    uhd::set_thread_priority_safe();
}

void pinThreadToCPU(pthread_t t, int cpu_num)
{
    RaiseCaps caps({CAP_SYS_NICE});
    cpu_set_t cpuset;
    int       ret;

    CPU_ZERO(&cpuset);
    CPU_SET(cpu_num, &cpuset);

    ret = pthread_setaffinity_np(t, sizeof(cpu_set_t), &cpuset);
    if (ret != 0)
        logScheduler(LOGERROR, "pthread_setaffinity_np: %s; error=%d",
            strerror(ret),
            ret);
}

void pinThisThread(void)
{
    static std::atomic<unsigned> npinned = 0;
    unsigned                     num_cpus = std::thread::hardware_concurrency();

    pinThreadToCPU(pthread_self(), npinned++ % num_cpus);
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
