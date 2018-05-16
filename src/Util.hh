#ifndef UTIL_HH_
#define UTIL_HH_

#include <thread>

/** @brief Execute a shell command specified with a printf-style format using
 * system.
 */
int sys(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** @brief Sleep for the specified number of seconds. sleep, usleep, and
 * nanosleep were already taken, so this function is named "doze."
 * @param sec The number of seconds to sleep.
 * @returns -1 if interrupted.
 */
int doze(double sec);

/** @brief Make the current thread wakeable. */
/** This function installs a signal handler for SIGUSR1 so that the thread can
 * be woken from a blocking sycall via wakeThread.
 */
void makeThreadWakeable(void);

/** @brief Wake the given thread. */
/** This function sends a SIGUSR1 signal to the given thread to wake it from any
 * blocking syscalls.
 */
void wakeThread(std::thread& t);

#endif    // UTIL_HH_
