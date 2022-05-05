// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <stdarg.h>

#include <memory>

#include "util/ssprintf.hh"

std::string ssprintf(const char *fmt, ...)
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
