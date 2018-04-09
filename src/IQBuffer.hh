#ifndef IQBUFFER_H_
#define IQBUFFER_H_

#include <complex>
#include <deque>
#include <memory>
#include <vector>

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

private:
    std::vector<std::complex<float>> data;
};

/** A queue of IQ samples */
using IQQueue = std::deque<std::shared_ptr<IQBuf>>;

#endif /* IQBUFFER_H_ */
