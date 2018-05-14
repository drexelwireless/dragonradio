#ifndef UTIL_HH_
#define UTIL_HH_

int sys(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** @brief Sleep for the specified number of seconds. sleep, usleep, and
 * nanosleep were already taken, so this function is named "doze."
 * @param sec The number of seconds to sleep.
 * @returns -1 if interrupted.
 */
int doze(double sec);

#endif    // UTIL_HH_
