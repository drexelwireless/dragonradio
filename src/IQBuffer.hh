#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <complex>
#include <deque>
#include <memory>
#include <vector>

/** A buffer of IQ samples */
using IQBuf = std::vector<std::complex<float>>;

/** A queue of IQ samples */
using IQQueue = std::deque<std::shared_ptr<IQBuf>>;

#endif /* IQBUFFER_H_ */
