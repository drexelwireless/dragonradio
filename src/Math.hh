// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MATH_HH_
#define MATH_HH_

#include <limits>

/** @brief Convert a floating-point number to a fractional number */
void frap(double x, long maxden, long &num, long &den);

#endif /* MATH_HH_ */
