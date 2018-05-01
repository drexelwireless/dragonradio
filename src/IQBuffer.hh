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

/** A slice of a buffer of IQ samples */
class IQSlice {
public:
    IQSlice() :
        off(0),
        len(0)
    {
    }

    IQSlice(std::shared_ptr<IQBuf> buf) :
        buf(buf),
        off(0),
        len(buf->size())
    {
    }

    IQSlice(std::shared_ptr<IQBuf> buf, size_t off, size_t len) :
        buf(buf),
        off(off),
        len(len)
    {
    }

    IQSlice(const IQSlice& other) :
        buf(other.buf),
        off(0),
        len(buf->size())
    {
    }

    IQSlice(IQSlice&& other) noexcept :
        buf(std::move(other.buf)),
        off(other.off),
        len(other.len)
    {
    }

    ~IQSlice() noexcept
    {
    }

    IQSlice& operator=(const IQSlice& other)
    {
        IQSlice tmp(other);
        *this = std::move(tmp);
        return *this;
    }

    IQSlice& operator=(IQSlice&& other) noexcept
    {
        if (this == &other)
            return *this;

        buf = std::move(other.buf);
        off = other.off;
        len = other.len;

        return *this;
    }

    const std::complex<float>& operator[](int i) const
    {
        return (*buf)[off+i];
    }

    std::complex<float>& operator[](int i)
    {
        return (*buf)[off+i];
    }

    size_t size(void)
    {
        return len;
    }

    std::shared_ptr<IQBuf> buf;
    size_t                 off;
    size_t                 len;
};

/** A queue of IQ samples */
using IQQueue = std::deque<IQSlice>;

#endif /* IQBUFFER_H_ */
