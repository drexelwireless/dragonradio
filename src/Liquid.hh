#ifndef LIQUID_H_
#define LIQUID_H_

#include <mutex>

/** Creation of liquid objects is not re-rentrant, so we need to protect access
 * with a mutex.
 */
extern std::mutex liquid_mutex;

#endif /* LIQUID_H_ */
