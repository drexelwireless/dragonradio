// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "buffer.hh"
#include "Clock.hh"

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

    IQBuf(IQBuf &&other) noexcept
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

    template<class InputIt>
    IQBuf(InputIt first, InputIt last)
      : buffer(0)
      , delay(0)
      , undersample(0)
      , oversample(0)
    {
        for (auto it = first; it != last; ++it) {
            resize(size_+1);
            data_[size_-1] = *it;
        }

        nsamples.store(0, std::memory_order_release);
        complete.store(true, std::memory_order_release);
    }

    ~IQBuf() noexcept {}

    IQBuf& operator=(const IQBuf&) = delete;
    IQBuf& operator=(IQBuf&&) = delete;

    /** @brief Timestamp of the first sample */
    std::optional<MonoClock::time_point> timestamp;

    /** @brief Sequence number of current slot */
    unsigned seq;

    /** @brief Sample center frequency */
    float fc;

    /** @brief Sample rate */
    float fs;

    /** @brief Signal delay */
    size_t delay;

    /** @brief Number of samples received so far. */
    /** This value is valid until the buffer is marked complete. */
    std::atomic<size_t> nsamples;

    /** @brief Flag that is true when receive is completed. */
    std::atomic<bool> complete;

    /** @brief Offset from beginning of the current snapshot. */
    std::optional<ssize_t> snapshot_off;

    /** @brief Number of undersamples at the beginning of the buffer. That is,
     * this is how many samples we missed relative to the requested RX start
     * time at the beginning of the RX.
     */
    size_t undersample;

    /** @brief Number oversamples at the end of buffer. That is, this is how
     * many samples we missed relative to the requested RX end time at the end
     * of the RX.
     */
    size_t oversample;

    /** @brief Wait for the buffer to start filling. */
    void waitToStartFilling(void)
    {
        for (int spin_count = 0;; ++spin_count) {
            if (nsamples.load(std::memory_order_acquire) != 0 ||
                complete.load(std::memory_order_acquire))
                break;

            if (spin_count < 16)
                _mm_pause();
            else {
                std::this_thread::yield();
                spin_count = 0;
            }
        }
    }

    /** @brief Zero all data in the buffer */
    void zero(void)
    {
        std::fill(data(), data() + size(), 0);
    }

    /** @brief Apply a gain
     * @param g The multiplicative gain.
     */
    void gain(const float g)
    {
        if (g != 1.0f)
            xsimd::transform(data() + delay,
                             data() + delay + size(),
                             data() + delay,
                [&](const auto& x) { return x*g; });
    }

    /** @brief Compute peak and average power */
    void power(float &peak_power, float &avg_power)
    {
        size_t n = size();

        peak_power = 0;
        avg_power = 0;

        for (size_t i = delay; i < n; ++i) {
            std::complex<float> x = (*this)[i];

            float norm = x.real()*x.real() + x.imag()*x.imag();

            if (norm > peak_power)
                peak_power = norm;

            avg_power += norm;
        }

        avg_power /= n;
    }
};

#endif /* IQBUFFER_H_ */
