// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LIQUID_MUTEX_HH_
#define LIQUID_MUTEX_HH_

#include <mutex>

namespace liquid {

/** @brief Creation of liquid objects is not re-rentrant, so we need to protect
 * access with a mutex.
 */
extern std::mutex mutex;

}

#endif /* LIQUID_MUTEX_HH_ */
