// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LOGGING_HH_
#define LOGGING_HH_

#include <stdarg.h>

#include "Clock.hh"

void vlogEvent(const Clock::time_point& t, const char *fmt, va_list ap);

void logEventAt(const Clock::time_point& t, const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 2, 3)))
#endif
;

inline void logEventAt(const Clock::time_point& t, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlogEvent(t, fmt, ap);
    va_end(ap);
}

void logEvent(const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 1, 2)))
#endif
;

inline void logEvent(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vlogEvent(Clock::now(), fmt, ap);
    va_end(ap);
}

#endif /* LOGGING_HH_ */
