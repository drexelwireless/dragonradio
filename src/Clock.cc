// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Clock.hh"

uhd::usrp::multi_usrp::sptr Clock::usrp_;

uhd::time_spec_t Clock::t0_(0.0);

double WallClock::skew_(1.0);

uhd::time_spec_t WallClock::offset_(0.0);
