#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>

#include "Logger.hh"
#include "RadioConfig.hh"
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
