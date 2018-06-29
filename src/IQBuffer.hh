#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <vector>

#include <uhd/types/time_spec.hpp>

#include "buffer.hh"
#include "Clock.hh"

/** @brief A buffer of IQ samples */
struct IQBuf : buffer<std::complex<float>> {
public:
    IQBuf(size_t sz) : buffer(sz)
    {
        nsamples.store(0, std::memory_order_release);
    }

    ~IQBuf() noexcept {}

    IQBuf(const IQBuf&) = delete;
    IQBuf(IQBuf&&) = delete;

    IQBuf& operator=(const IQBuf&) = delete;
    IQBuf& operator=(IQBuf&&) = delete;

    /** @brief Timestamp of the first sample */
    Clock::time_point timestamp;

    /** @brief Number of samples received so far. */
    /** This value is valid untile the buffer is marked complete. */
    std::atomic<size_t> nsamples;

    /** @brief Number of undersamples at the beginning of the buffer. That is,
     * this is how many samples we missed at the beginning of the receive.
     */
    size_t undersample;

    /** @brief Number oversamples at the end of buffer. */
    size_t oversample;
};

#endif /* IQBUFFER_H_ */
