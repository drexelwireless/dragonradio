// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "logging.hh"
#include "Logger.hh"
#include "RadioConfig.hh"

void vlogEvent(const Clock::time_point& t, const char *fmt, va_list ap0)
{
    std::shared_ptr<Logger> l = logger;

    if (rc.debug || (l && l->getCollectSource(Logger::kEvents))) {
        int                     n = 2 * strlen(fmt);
        std::unique_ptr<char[]> buf;
        va_list                 ap;

        for (;;) {
            buf.reset(new char[n]);

            va_copy(ap, ap0);
            int count = vsnprintf(&buf[0], n, fmt, ap);
            va_end(ap);

            if (count < 0 || count >= n)
                n *= 2;
            else
                break;
        }

        std::string s { buf.get() };

        if (rc.debug)
            fprintf(stderr, "%s\n", s.c_str());

        if (l && l->getCollectSource(Logger::kEvents))
            l->logEvent(t, s);
    }
}
