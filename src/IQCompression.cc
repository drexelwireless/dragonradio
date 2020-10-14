// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "IQCompression.hh"
#include "IQCompression/FLAC.hh"

void convert2sc16(const fc32_t *from, sc16_t *to, size_t n)
{
    constexpr float k = 32767.;
    const float     *in = reinterpret_cast<const float*>(from);
    int16_t         *out = reinterpret_cast<int16_t*>(to);

    for (size_t i = 0; i < 2*n; ++i)
        out[i] = in[i]*k;
}

void convert2fc32(const sc16_t *from, fc32_t *to, size_t n)
{
    constexpr float k = 1/32767.;
    const int16_t   *in = reinterpret_cast<const int16_t*>(from);
    float           *out = reinterpret_cast<float*>(to);

    for (size_t i = 0; i < 2*n; ++i)
        out[i] = in[i]*k;
}

class BufferEncoder : public FLACMemoryEncoder {
public:
    BufferEncoder() = default;

    virtual ~BufferEncoder() = default;

    class buffer<char> encoded;

protected:
    size_t size(void) override
    {
        return encoded.size();
    }

    char *data(void) override
    {
        return encoded.data();
    }

    void resize(size_t size) override
    {
        encoded.resize(size);
    }
};

class BufferDecoder : public FLACMemoryDecoder {
public:
    BufferDecoder() = default;

    virtual ~BufferDecoder() = default;

    buffer<fc32_t> decoded;

protected:
    size_t size(void) override
    {
        return decoded.size();
    }

    fc32_t *data(void) override
    {
        return decoded.data();
    }

    void resize(size_t size) override
    {
        decoded.resize(size);
    }
};

buffer<char> compressIQData(const fc32_t *data, size_t n)
{
    static BufferEncoder encoder;

    encoder.encode(data, n);

    return std::move(encoder.encoded);
}

buffer<fc32_t> decompressIQData(const char *data, size_t n)
{
    static BufferDecoder decoder;

    decoder.decode(data, n);
    return std::move(decoder.decoded);
}
