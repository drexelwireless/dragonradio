#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <optional>
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
      , undersample(0)
      , oversample(0)
    {
        nsamples.store(0, std::memory_order_release);
        complete.store(false, std::memory_order_release);
    }

    IQBuf(const IQBuf &other)
      : buffer(other)
      , delay(other.delay)
      , snapshot_off(other.snapshot_off)
      , undersample(other.undersample)
      , oversample(other.oversample)
    {
        nsamples.store(other.nsamples.load(std::memory_order_acquire),
            std::memory_order_release);
        complete.store(other.complete.load(std::memory_order_acquire),
            std::memory_order_release);
    }

    IQBuf(IQBuf &&other)
      : buffer(std::move(other))
      , delay(other.delay)
      , snapshot_off(other.snapshot_off)
      , undersample(other.undersample)
      , oversample(other.oversample)
    {
        nsamples.store(other.nsamples.load(std::memory_order_acquire),
            std::memory_order_release);
        complete.store(other.complete.load(std::memory_order_acquire),
            std::memory_order_release);
    }

    IQBuf(const buffer<std::complex<float>> &other)
      : buffer(other)
      , delay(0)
      , undersample(0)
      , oversample(0)
    {
        nsamples.store(0, std::memory_order_release);
        complete.store(true, std::memory_order_release);
    }

    IQBuf(buffer<std::complex<float>> &&other)
      : buffer(std::move(other))
      , delay(0)
      , undersample(0)
      , oversample(0)
    {
        nsamples.store(0, std::memory_order_release);
        complete.store(true, std::memory_order_release);
    }

    IQBuf(const std::complex<float> *data, size_t n)
      : buffer(data, n)
      , delay(0)
      , undersample(0)
      , oversample(0)
    {
        nsamples.store(0, std::memory_order_release);
        complete.store(true, std::memory_order_release);
    }

    ~IQBuf() noexcept {}

    IQBuf& operator=(const IQBuf&) = delete;
    IQBuf& operator=(IQBuf&&) = delete;

#if !defined(NOUHD)
    /** @brief Timestamp of the first sample */
    MonoClock::time_point timestamp;
#endif /* !defined(NOUHD) */

    /** @brief Sample center frequency */
    float fc;

    /** @brief Sample rate */
    float fs;

    /** @brief Signal delay */
    size_t delay;

    /** @brief Number of samples received so far. */
    /** This value is valid untile the buffer is marked complete. */
    std::atomic<size_t> nsamples;

    /** @brief Flag that is true when receive is completed. */
    std::atomic<bool> complete;

    /** @brief Offset from beginning of the current snapshot. */
    std::optional<size_t> snapshot_off;

    /** @brief Number of undersamples at the beginning of the buffer. That is,
     * this is how many samples we missed at the beginning of the receive.
     */
    size_t undersample;

    /** @brief Number oversamples at the end of buffer. */
    size_t oversample;
};

#endif /* IQBUFFER_H_ */
