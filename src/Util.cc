#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>

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

    if (rc.verbose)
        printf("%s\n", cmd);
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

static void dummySignalHandler(int)
{
}

void makeThreadWakeable(void)
{
    struct sigaction sa = {{0}};

    sa.sa_handler = dummySignalHandler;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("makeThreadWakeable() failed");
        exit(1);
    }
}

void wakeThread(std::thread& t)
{
    pthread_kill(t.native_handle(), SIGUSR1);
}
