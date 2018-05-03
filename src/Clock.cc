#include <sys/time.h>

#include "Clock.hh"

uhd::usrp::multi_usrp::sptr Clock::_usrp;

void Clock::setUSRP(uhd::usrp::multi_usrp::sptr usrp)
{
    _usrp = usrp;

    // Set USRP time relative to system NTP time
    timeval tv;

    gettimeofday(&tv, NULL);

    usrp->set_time_now(uhd::time_spec_t(tv.tv_sec, ((double)tv.tv_usec)/1e6));
}

void Clock::releaseUSRP(void)
{
    _usrp.reset();
}
