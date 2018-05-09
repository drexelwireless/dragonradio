#include <sys/time.h>

#include "Clock.hh"

uhd::usrp::multi_usrp::sptr Clock::usrp_;

void Clock::setUSRP(uhd::usrp::multi_usrp::sptr usrp)
{
    usrp_ = usrp;

    // Set USRP time relative to system NTP time
    timeval tv;

    gettimeofday(&tv, NULL);

    usrp->set_time_now(uhd::time_spec_t(tv.tv_sec, ((double)tv.tv_usec)/1e6));
}

void Clock::releaseUSRP(void)
{
    usrp_.reset();
}
