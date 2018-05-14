#ifndef GAIN_HH_
#define GAIN_HH_

#include "IQBuffer.hh"

/** @brief Calculate soft TX gain necessary for 0 dBFS.
 * @param buf The IQ buffer for which we are calculating soft gain.
 * @param clip_frac The fraction of IQ values we must not clip. For example, a
 * value of 0.99 ensures that 99% of the values will fall below 1, i.e., the
 * 99th percentile is unclipped.
 * @returns The *multiplicative* gain necessary for 0 dBFS
 */
float autoSoftGain0dBFS(IQBuf& buf, float clip_frac);

#endif /* GAIN_HH_ */
