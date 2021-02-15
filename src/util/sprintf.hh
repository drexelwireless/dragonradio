// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_SPRINTF_HH_
#define UTIL_SPRINTF_HH_

#include <string>

/** @brief sprintf to a std::string. */
std::string sprintf(const char *fmt, ...)
#if !defined(DOXYGEN)
__attribute__((format(printf, 1, 2)))
#endif
;

#endif /* UTIL_SPRINTF_HH_ */
