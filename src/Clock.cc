// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    struct timespec t;
    int    err;

    if ((err = clock_gettime(CLOCK_REALTIME, &t)) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    uhd::time_spec_t now(t.tv_sec, ((double)t.tv_nsec)/1e9);

    usrp_ = usrp;
    t0_ = now;

#ifdef RANDOM_CLOCK_BIAS
    std::random_device rd;
    std::mt19937       gen(rd());
    std::uniform_real_distribution<> dist(0.0, 10.0);
    double offset = dist(gen);

    fprintf(stderr, "CLOCK: offset=%g\n", offset);

    Clock::setTimeNow(t0_ + offset);
#else
    Clock::setTimeNow(t0_);
#endif
}

void Clock::releaseUSRP(void)
{
    usrp_.reset();
}
