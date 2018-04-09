#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <complex>
#include <deque>
#include <memory>
#include <vector>

#include <uhd/types/time_spec.hpp>

/** A buffer of IQ samples */
class IQBuf {
public:
    IQBuf()
    {
    }

    IQBuf(size_t sz) :
        data(sz)
    {
    }

    IQBuf(const IQBuf& other) :
        data(other.data)
    {
    }

    IQBuf(IQBuf&& other) noexcept :
        data(std::move(other.data))
    {
    }

    ~IQBuf() noexcept
    {
    }

    IQBuf& operator=(const IQBuf& other)
    {
        IQBuf tmp(other);
        *this = std::move(tmp);
        return *this;
    }

    IQBuf& operator=(IQBuf&& other) noexcept
    {
        if (this == &other)
            return *this;

        data = std::move(other.data);

        return *this;
    }

    const std::complex<float>& operator[](int i) const
    {
        return data[i];
    }

    std::complex<float>& operator[](int i)
    {
        return data[i];
    }

    size_t size(void)
    {
        return data.size();
    }

    void resize(size_t sz)
    {
        data.resize(sz);
    }

    void set_timestamp(uhd::time_spec_t t)
    {
        timestamp = t;
    }

    uhd::time_spec_t get_timestamp(void)
    {
        return timestamp;
    }

private:
    /** IQ samples */
    std::vector<std::complex<float>> data;

    /** Timestamp of the first sample */
    uhd::time_spec_t timestamp;
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

private:
    std::shared_ptr<IQBuf> buf;
    size_t                 off;
    size_t                 len;
};

/** A queue of IQ samples */
using IQQueue = std::deque<IQSlice>;

#endif /* IQBUFFER_H_ */
