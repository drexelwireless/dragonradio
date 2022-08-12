// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MATH_HH_
#define MATH_HH_

#include <limits>

/** @brief Convert a floating-point number to a fractional number */
void frap(double x, long maxden, long &num, long &den);

/** @brief Unwrap phase to [0, 2*M_PI)
 * @param x Phase
 * @return wrapped phase
 */
inline double unwrapPhase(double x)
{
    // This rigamarole is necessary to handle negative values.
    return fmod(2.0*M_PI + fmod(x, 2.0*M_PI), 2.0*M_PI);
}

#endif /* MATH_HH_ */
