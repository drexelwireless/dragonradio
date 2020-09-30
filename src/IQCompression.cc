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
    BufferEncoder()
    {
    }

    virtual ~BufferEncoder() = default;

    /** @brief Buffer for encoded data */
    buffer<char> encoded_bytes;

protected:
    size_t size(void) override
    {
        return encoded_bytes.size();
    }

    char *data(void) override
    {
        return encoded_bytes.data();
    }

    void resize(size_t size) override
    {
        encoded_bytes.resize(size);
    }
};

class BufferDecoder : public FLACMemoryDecoder {
public:
    BufferDecoder(const char *encoded, size_t n)
      : FLACMemoryDecoder(encoded, n)
    {
    }

    virtual ~BufferDecoder() = default;

    /** @brief Buffer for decoded signal */
    buffer<fc32_t> decoded_sig;

protected:
    size_t size(void) override
    {
        return decoded_sig.size();
    }

    float *data(void) override
    {
        return reinterpret_cast<float*>(decoded_sig.data());
    }

    void resize(size_t size) override
    {
        decoded_sig.resize(size);
    }
};

buffer<char> compressFLAC(unsigned compression_level, const fc32_t *data, size_t n)
{
    BufferEncoder encoder;

    encoder.encode(compression_level,
                   data,
                   n);

    return encoder.encoded_bytes;
}

/** @brief Decompress FLAC-encoded fc32 data */
buffer<fc32_t> decompressFLAC(const char *data, size_t n)
{
    BufferDecoder decoder(data, n);

    decoder.decode();

    return decoder.decoded_sig;
}
