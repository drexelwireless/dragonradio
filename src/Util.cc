// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <sys/wait.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include <uhd/utils/thread_priority.hpp>

#include "logging.hh"
#include "Util.hh"

std::string sprintf(const char *fmt, ...)
{
    int                     n = 1024;
    std::unique_ptr<char[]> buf;
    va_list                 ap;

    for (;;) {
        buf.reset(new char[n]);

        va_start(ap, fmt);
        int count = vsnprintf(&buf[0], n, fmt, ap);
        va_end(ap);

        if (count < 0 || count >= n)
            n *= 2;
        else
            break;
    }

    return std::string(buf.get());
}

int exec(const std::vector<std::string>& args)
{
    std::string command(args[0]);
    pid_t       pid;
    int         wstatus;

    for (auto arg = ++args.begin(); arg != args.end(); arg++) {
        command += " ";
        command += *arg;
    }

    logSystem(LOGDEBUG, "%s", command.c_str());

    if ((pid = fork()) < 0) {
        throw std::runtime_error(strerror(errno));
    } else if (pid == 0) {
        std::vector<const char*> cargs(args.size() + 1);

        for (std::vector<const char*>::size_type i = 0; i < args.size(); ++i)
            cargs[i] = args[i].c_str();

        if (execvp(cargs[0], const_cast<char* const*>(&cargs[0])) < 0) {
            throw std::runtime_error(strerror(errno));
        }

        return 0;
    } else {
        waitpid(pid, &wstatus, 0);

        if (wstatus != 0)
            logSystem(LOGDEBUG, "%s (%d)", command.c_str(), wstatus);

        return wstatus;
    }
}

void setRealtimePriority(pthread_t t)
{
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
