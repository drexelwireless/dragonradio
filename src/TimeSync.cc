#include "RadioConfig.hh"
#include "TimeSync.hh"

/** @brief Record a timestamp from this node */
void TimeInfo::updateTimestamp(const ControlMsg::Timestamp &msg, const Clock::time_point &our_time)
{
    saw_timestamp = true;
    last_timestamp_epoch = msg.epoch;
    last_timestamp_delta = our_time - msg.t.to_wall_time();
    last_timestamp_our_time = our_time;
}
