#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <complex>
#include <deque>
#include <memory>
#include <vector>

#include <uhd/types/time_spec.hpp>

#include "buffer.hh"

/** @brief A buffer of IQ samples */
struct IQBuf : buffer<std::complex<float>> {
public:
    IQBuf(size_t sz) : buffer(sz) {}

    ~IQBuf() noexcept {}

    IQBuf(const IQBuf&) = delete;
    IQBuf(IQBuf&&) = delete;

    IQBuf& operator=(const IQBuf&) = delete;
    IQBuf& operator=(IQBuf&&) = delete;

    /** @brief Timestamp of the first sample */
    uhd::time_spec_t timestamp;

    /** @brief Number of samples by which we oversampled. */
    size_t oversample;
};

#endif /* IQBUFFER_H_ */
