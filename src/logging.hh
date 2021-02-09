// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LOGGING_HH_
#define LOGGING_HH_

#include <stdarg.h>

#include "Clock.hh"
#include "Logger.hh"

using loglevel = unsigned;

const loglevel LOGCRITICAL = 50;
const loglevel LOGERROR = 40;
const loglevel LOGWARNING = 30;
const loglevel LOGINFO = 20;
const loglevel LOGDEBUG = 10;
const loglevel LOGNOTSET = 0;

/** @brief Event categories */
enum EventCategory {
    kEventSystem = 0,
    kEventScheduler,
    kEventNet,
    kEventTunTap,
    kEventTimeSync,
    kEventAMC,
    kEventARQ,
    kEventMAC,
    kEventPHY,
    kEventUSRP,
    kNumEvents
};

/** @brief Event category log levels */
extern loglevel loglevels[kNumEvents];

/** @brief Event category log print levels */
extern loglevel printlevels[kNumEvents];

/** @brief Return the string name of an event category */
std::string eventCategory2string(EventCategory cat);

/** @brief Return the named event category*/
EventCategory string2EventCategory(const std::string &s);

/** @brief Return true if logging is enabled for level */
bool isLogLevelEnabled(EventCategory, loglevel);

/** @brief Set log level */
void setLogLevel(EventCategory, loglevel);

/** @brief Return true if printing is enabled for level */
bool isPrintLogLevelEnabled(EventCategory, loglevel);

/** @brief Set printing log level */
void setPrintLogLevel(EventCategory, loglevel);

/** @brief Log an event */
void vlogEvent(const WallClock::time_point& t,
               EventCategory cat,
               loglevel lvl,
               const char *fmt,
               va_list ap);

void logEvent(EventCategory cat,
              loglevel lvl,
              const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 3, 4)))
#endif
;

/** @brief Log an event using current time */
inline void logEvent(EventCategory cat,
                     loglevel lvl,
                     const char *fmt, ...)
{
    if (lvl >= loglevels[cat]) {
        va_list ap;

        va_start(ap, fmt);
        vlogEvent(WallClock::now(), cat, lvl, fmt, ap);
        va_end(ap);
    }
}

/** @brief Log an event at specific time */
void logEventAt(const WallClock::time_point& t,
                EventCategory cat,
                loglevel lvl,
                const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 4, 5)))
#endif
;

inline void logEventAt(const WallClock::time_point& t,
                       EventCategory cat,
                       loglevel lvl,
                       const char *fmt, ...)
{
    if (lvl >= loglevels[cat]) {
        va_list ap;

        va_start(ap, fmt);
        vlogEvent(t, cat, lvl, fmt, ap);
        va_end(ap);
    }
}

#define logSystem(lvl, ...)    logEvent(kEventSystem, lvl, "SYSTEM: " __VA_ARGS__)
#define logScheduler(lvl, ...) logEvent(kEventScheduler, lvl, "SCHEDULER: " __VA_ARGS__)
#define logNet(lvl, ...)       logEvent(kEventNet, lvl, "NET: " __VA_ARGS__)
#define logTunTap(lvl, ...)    logEvent(kEventTunTap, lvl, "TUNTAP: " __VA_ARGS__)
#define logTimeSync(lvl, ...)  logEvent(kEventTimeSync, lvl, "TIMESYNC: " __VA_ARGS__)
#define logAMC(lvl, ...)       logEvent(kEventAMC, lvl, "AMC: " __VA_ARGS__)
#define logARQ(lvl, ...)       logEvent(kEventARQ, lvl, "ARQ: " __VA_ARGS__)
#define logMAC(lvl, ...)       logEvent(kEventMAC, lvl, "MAC: " __VA_ARGS__)
#define logPHY(lvl, ...)       logEvent(kEventPHY, lvl, "PHY: " __VA_ARGS__)
#define logUSRP(lvl, ...)      logEvent(kEventUSRP, lvl, "USRP: " __VA_ARGS__)
#define logUSRPAt(t, lvl, ...) logEventAt(t, kEventUSRP, lvl, "USRP: " __VA_ARGS__)

#endif /* LOGGING_HH_ */
