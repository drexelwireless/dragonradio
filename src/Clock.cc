#include <sys/time.h>

#include "Clock.hh"

uhd::time_spec_t MonoClock::t0_(0.0);

uhd::usrp::multi_usrp::sptr MonoClock::usrp_;

double Clock::skew_(1.0);

uhd::time_spec_t Clock::offset_(0.0);

void Clock::setUSRP(uhd::usrp::multi_usrp::sptr usrp)
{
    // Set offset relative to system NTP time
    timeval tv;

    gettimeofday(&tv, NULL);

    uhd::time_spec_t now(tv.tv_sec, ((double)tv.tv_usec)/1e6);

    usrp_ = usrp;
    t0_ = now;

    usrp->set_time_now(t0_);
}

void Clock::releaseUSRP(void)
{
    usrp_.reset();
}
