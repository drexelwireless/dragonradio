// Copyright 2018-2021 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <chrono>
#include <memory>

using namespace std::literals::chrono_literals;

#include "Clock.hh"

std::shared_ptr<MonoClock::TimeKeeper> MonoClock::time_keeper_;

MonoClock::time_point MonoClock::t0_;

WallClock::duration WallClock::offset_ = 0.0s;

double WallClock::skew_ = 1.0;
