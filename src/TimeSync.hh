#ifndef TIMESYNC_HH_
#define TIMESYNC_HH_

#include "Packet.hh"
#include "stats/Estimator.hh"

/** @brief Contains time information for a particular node */
struct TimeInfo {
    TimeInfo () : saw_timestamp(false) {}

    /** @brief Have we seen a timestamp from this node? */
    bool saw_timestamp;

    /** @brief Last seen timestamp epoch from this node */
    Seq last_timestamp_epoch;

    /** @brief Delta of last timestamp. */
    Clock::time_point last_timestamp_delta;

    /** @brief Out time at last timestamp. */
    Clock::time_point last_timestamp_our_time;

    /** @brief Record a timestamp */
    void updateTimestamp(const ControlMsg::Timestamp &msg, const Clock::time_point &our_time);
};

/** @brief Contains time sync information for a this node */
struct TimeSync {
    TimeSync()
      : time_master(0)
      // We want the last 10 entries to account for 86% of the EMA
      , skew(2.0/(10.0 + 1.0))
    {}

    /** @brief Node ID from which we get our clock */
    NodeId time_master;

    /** @brief Last time we adjusted our clock */
    Clock::time_point last_adjustment;

    /** @brief Estimated clock skew */
    EMA<double> skew;
};

#endif /* TIMESYNC_HH_ */
