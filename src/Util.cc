#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

    printf("%s\n", cmd);
    return res;
}
