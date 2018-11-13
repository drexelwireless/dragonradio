#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <vector>

#if !defined(NOUHD)
#include <uhd/types/time_spec.hpp>
#endif /* !defined(NOUHD) */

#include "buffer.hh"

#if !defined(NOUHD)
#include "Clock.hh"
#endif /* !defined(NOUHD) */

/** @brief A buffer of IQ samples */
struct IQBuf : buffer<std::complex<float>> {
public:
    IQBuf(size_t sz)
      : buffer(sz)
      , delay(0)
      , complete(false)
      , undersample(0)
      , oversample(0)
    {
        nsamples.store(0, std::memory_order_release);
    }

    IQBuf(const IQBuf &other)
      : buffer(other)
      , delay(other.delay)
      , complete(other.complete)
      , undersample(other.undersample)
      , oversample(other.oversample)
    {
        nsamples.store(other.nsamples.load());
    }

    IQBuf(IQBuf &&other)
      : buffer(std::move(other))
      , delay(other.delay)
      , complete(other.complete)
      , undersample(other.undersample)
      , oversample(other.oversample)
    {
        nsamples.store(other.nsamples.load());
    }

    ~IQBuf() noexcept {}

    IQBuf& operator=(const IQBuf&) = delete;
    IQBuf& operator=(IQBuf&&) = delete;

#if !defined(NOUHD)
    /** @brief Timestamp of the first sample */
    Clock::time_point timestamp;
#endif /* !defined(NOUHD) */

    /** @brief Signal delay */
    size_t delay;

    /** @brief Number of samples received so far. */
    /** This value is valid untile the buffer is marked complete. */
    std::atomic<size_t> nsamples;

    /** @brief Flag that is true when receive is completed. */
    bool complete;

    /** @brief Number of undersamples at the beginning of the buffer. That is,
     * this is how many samples we missed at the beginning of the receive.
     */
    size_t undersample;

    /** @brief Number oversamples at the end of buffer. */
    size_t oversample;
};

#endif /* IQBUFFER_H_ */
