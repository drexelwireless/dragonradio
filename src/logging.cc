// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "logging.hh"
#include "Logger.hh"
#include "RadioConfig.hh"

constexpr size_t MAXLEN = 1024;

void vlogEvent(const Clock::time_point& t, const char *fmt, va_list ap)
{
    std::shared_ptr<Logger> l = logger;

    if (rc.debug || (l && l->getCollectSource(Logger::kEvents))) {
        std::unique_ptr<char[]> buf(new char[MAXLEN]);

        if (vsnprintf(&buf[0], MAXLEN, fmt, ap) > 0) {
            if (rc.debug) {
                fputs(buf.get(), stderr);
                putc('\n', stderr);
            }

            if (l && l->getCollectSource(Logger::kEvents))
                l->logEvent(t, std::move(buf));
        }
    }
}
