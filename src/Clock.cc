#include <sys/time.h>

#include "Clock.hh"

uhd::usrp::multi_usrp::sptr MonoClock::usrp_;

Seq Clock::epoch_(0);

double Clock::skew_ = 0.;

uhd::time_spec_t Clock::offset_;

Clock::time_point Clock::last_adjustment_;

void Clock::setUSRP(uhd::usrp::multi_usrp::sptr usrp)
{
    // Set offset relative to system NTP time
    timeval tv;

    gettimeofday(&tv, NULL);

    offset_ = uhd::time_spec_t(tv.tv_sec, ((double)tv.tv_usec)/1e6);

    usrp_ = usrp;

    usrp->set_time_now(0.0);
}

void Clock::releaseUSRP(void)
{
    usrp_.reset();
}
