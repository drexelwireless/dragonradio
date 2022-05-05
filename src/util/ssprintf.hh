// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_SSPRINTF_HH_
#define UTIL_SSPRINTF_HH_

#include <string>

/** @brief sprintf to a std::string. */
std::string ssprintf(const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 1, 2)))
#endif
;

#endif /* UTIL_SSPRINTF_HH_ */
