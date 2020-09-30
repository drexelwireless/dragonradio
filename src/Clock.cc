// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/time.h>

#ifdef RANDOM_CLOCK_BIAS
#include <random>
#endif

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

#ifdef RANDOM_CLOCK_BIAS
    std::random_device rd;
    std::mt19937       gen(rd());
    std::uniform_real_distribution<> dist(0.0, 10.0);
    double offset = dist(gen);

    fprintf(stderr, "CLOCK: offset=%g\n", offset);

    usrp->set_time_now(t0_ + offset);
#else
    usrp->set_time_now(t0_);
#endif
}

void Clock::releaseUSRP(void)
{
    usrp_.reset();
}
