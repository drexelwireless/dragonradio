// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>
#include <cassert>

#include "logging.hh"
#include "Logger.hh"

loglevel loglevels[kNumEvents] = {0};

loglevel printlevels[kNumEvents] = {0};

bool isLogLevelEnabled(EventCategory cat, loglevel lvl)
{
    assert(cat < kNumEvents);
    return lvl >= loglevels[cat];
}

void setLogLevel(EventCategory cat, loglevel lvl)
{
    assert(cat < kNumEvents);
    loglevels[cat] = lvl;
}

bool isPrintLogLevelEnabled(EventCategory cat, loglevel lvl)
{
    assert(cat < kNumEvents);
    return lvl >= printlevels[cat];
}

void setPrintLogLevel(EventCategory cat, loglevel lvl)
{
    assert(cat < kNumEvents);
    printlevels[cat] = lvl;
}

static const char *kEventCategoryString[] =
    { "SYSTEM"
    , "SCHEDULER"
    , "NET"
    , "TUNTAP"
    , "TIMESYNC"
    , "AMC"
    , "ARQ"
    , "MAC"
    , "PHY"
    , "USRP"
    };

std::string eventCategory2string(EventCategory cat)
{
    if (cat < kNumEvents)
        return kEventCategoryString[cat];

    throw std::range_error("Unknown event category");
}

EventCategory string2EventCategory(const std::string &s)
{
    for (unsigned i = 0; i < kNumEvents; ++i ) {
        if (!strcmp(kEventCategoryString[i], s.c_str()))
            return static_cast<EventCategory>(i);
    }

    throw std::range_error("Unknown event category");
}

constexpr size_t MAXLEN = 1024;

void vlogEvent(const Clock::time_point& t,
               EventCategory cat,
               loglevel lvl,
               const char *fmt,
               va_list ap)
{
    if (logger) {
        std::unique_ptr<char[]> buf(new char[MAXLEN]);

        vsnprintf(&buf[0], MAXLEN, fmt, ap);

        if (lvl >= printlevels[cat]) {
            fputs(buf.get(), stderr);
            putc('\n', stderr);
        }

        logger->logEvent(t, std::move(buf));
    }
}
