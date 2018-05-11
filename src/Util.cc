#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

    if (rc->verbose)
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
