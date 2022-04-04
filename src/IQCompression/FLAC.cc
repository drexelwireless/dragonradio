// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>
#include <xsimd/xsimd.hpp>

#include "IQCompression.hh"
#include "IQCompression/FLAC.hh"

// The X310 AD units only provides 14 bits. We do not get 14 bits out of it, but
// we certainly don't get more than 14 :)
constexpr unsigned kBits = 14;

/** @brief Convert fc32_t IQ data to interleaved int32 format */
void convert2int32(const fc32_t *in_, const size_t n, int32_t *out)
{
    using vec_type = xsimd::simd_type<float>;

    constexpr float k = 1 << (kBits - 1);
    const vec_type  kvec(k);

    const float *in = reinterpret_cast<const float*>(in_);

    size_t size = 2*n;
    size_t inc = vec_type::size;
    size_t vec_size = size - size % inc;

    for (size_t i = 0; i < vec_size; i += inc) {
        vec_type invec = xsimd::load_unaligned(&in[i]);

        xsimd::store_unaligned(&out[i], xsimd::batch_cast<int32_t,float>(kvec*invec));
    }

    // Remaining part that cannot be vectorized
    for (size_t i = vec_size; i < size; ++i)
        out[i] = k*in[i];
}

template<class T>
void interleave(const T &c, const T &y, T *res);

template<>
void interleave<float>(const float &x, const float &y, float *res)
{
    res[0] = x;
    res[1] = y;
}

#if defined(__AVX2__)
// For the inverse operations on 32-bit integers, see:
// https://stackoverflow.com/questions/42497985/packing-and-de-interleaving-two-m256-registers
template<>
void interleave<xsimd::batch<float, xsimd::avx2>>(const xsimd::batch<float, xsimd::avx2> &lo,
                                                  const xsimd::batch<float, xsimd::avx2> &hi,
                                                  xsimd::batch<float, xsimd::avx2> *res_)
{
    auto lo_grouped = _mm256_permute2f128_ps(lo, hi, 0 | (2 << 4));
    auto hi_grouped = _mm256_permute2f128_ps(lo, hi, 1 | (3 << 4));

    const __m256i mask = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);

    _mm256_storeu_ps(reinterpret_cast<float*>(&res_[0]), _mm256_permutevar8x32_ps(lo_grouped, mask));
    _mm256_storeu_ps(reinterpret_cast<float*>(&res_[1]), _mm256_permutevar8x32_ps(hi_grouped, mask));
}
#else /* !defined(__AVX2__) */
template<>
void interleave<xsimd::simd_type<float>>(const xsimd::simd_type<float> &x, const xsimd::simd_type<float> &y, xsimd::simd_type<float> *res_)
{
    float *res = reinterpret_cast<float*>(res_);

    for (size_t j = 0; j < xsimd::simd_type<float>::size; ++j) {
        res[2*j] = x.get(j);
        res[2*j+1] = y.get(j);
    }
}
#endif /* !defined(__AVX2__) */

/** @brief Convert non-interleaved int32 format to fc32 */
void convert2fc32(const int32_t *const in[], const size_t size, fc32_t *out_)
{
#if defined(__AVX2__)
    using ivec_type = xsimd::batch<int32_t, xsimd::avx2>;
    using fvec_type = xsimd::batch<float, xsimd::avx2>;
#else /* !defined(__AVX2__) */
    using ivec_type = xsimd::simd_type<int32_t>;
    using fvec_type = xsimd::simd_type<float>;
#endif /* !defined(__AVX2__) */

    constexpr float k = 1.f/(1 << (kBits - 1));
    const fvec_type  kvec(k);

    size_t inc = fvec_type::size;
    size_t vec_size = size - size % inc;

    for (size_t i = 0; i < vec_size; i += inc) {
        fvec_type rvec;
        fvec_type ivec;

        rvec = xsimd::batch_cast<float,int32_t>(ivec_type::load_unaligned<int32_t>(&in[0][i]));
        ivec = xsimd::batch_cast<float,int32_t>(ivec_type::load_unaligned<int32_t>(&in[1][i]));

        interleave(kvec*rvec, kvec*ivec, reinterpret_cast<fvec_type*>(&out_[i]));
    }

    // Remaining part that cannot be vectorized
    for (size_t i = vec_size; i < size; ++i)
        interleave(k*in[0][i], k*in[1][i], reinterpret_cast<float*>(&out_[i]));
}

void FLACMemoryEncoder::encode(const fc32_t *sig, size_t n)
{
    off_ = 0;

    check(is_valid());
    //check(set_verify(true));
    check(set_compression_level(3));
    check(set_channels(2));
    check(set_bits_per_sample(kBits));
    check(set_do_mid_side_stereo(false));
    check(set_streamable_subset(false));
    check(set_total_samples_estimate(n));

    checkInit(init());

    std::unique_ptr<int32_t[]> tempbuf = std::unique_ptr<int32_t[]>(new int32_t[2*n]);

    convert2int32(sig, n, tempbuf.get());

    check(process_interleaved(tempbuf.get(), n));

    check(finish());
}

FLAC__StreamEncoderReadStatus FLACMemoryEncoder::read_callback(FLAC__byte buffer[], size_t *bytes)
{
    return FLAC__STREAM_ENCODER_READ_STATUS_UNSUPPORTED;
}

FLAC__StreamEncoderWriteStatus FLACMemoryEncoder::write_callback(const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame)
{
    if (size() < off_ + bytes)
        resize(off_ + bytes);

    memcpy(data() + off_, buffer, bytes);
    off_ += bytes;

    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderSeekStatus FLACMemoryEncoder::seek_callback(FLAC__uint64 absolute_byte_offset)
{
    if (absolute_byte_offset >= size())
        return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
    else {
        off_ = absolute_byte_offset;
        return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
    }
}

FLAC__StreamEncoderTellStatus FLACMemoryEncoder::tell_callback(FLAC__uint64 *absolute_byte_offset)
{
    *absolute_byte_offset = off_;
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

void FLACMemoryEncoder::metadata_callback(const FLAC__StreamMetadata *metadata)
{
}

FLACMemoryDecoder::FLACMemoryDecoder()
{
}

void FLACMemoryDecoder::decode(const char *encoded, size_t n)
{
    encoded_ = encoded;
    encoded_size_ = n;
    encoded_off_ = 0;
    off_ = 0;

    checkInit(init());

    check(process_until_end_of_stream());

    check(finish());
}

FLAC__StreamDecoderReadStatus FLACMemoryDecoder::read_callback(FLAC__byte buffer[], size_t *bytes)
{
    if (encoded_off_ == encoded_size_)
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    else {
        size_t n = std::min(*bytes, encoded_size_ - encoded_off_);

        memcpy(buffer, encoded_ + encoded_off_, n);
        *bytes = n;
        encoded_off_ += n;

        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }
}

FLAC__StreamDecoderSeekStatus FLACMemoryDecoder::seek_callback(FLAC__uint64 absolute_byte_offset)
{
    if (absolute_byte_offset >= encoded_size_)
        return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
    else {
        encoded_off_ = absolute_byte_offset;
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
}

FLAC__StreamDecoderTellStatus FLACMemoryDecoder::tell_callback(FLAC__uint64 *absolute_byte_offset)
{
    *absolute_byte_offset = encoded_off_;
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus FLACMemoryDecoder::length_callback(FLAC__uint64 *stream_length)
{
    *stream_length = encoded_size_;
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

bool FLACMemoryDecoder::eof_callback()
{
    return encoded_off_ == encoded_size_;
}

FLAC__StreamDecoderWriteStatus FLACMemoryDecoder::write_callback(const FLAC__Frame *frame, const FLAC__int32 *const buffer[])
{
    size_t n = frame->header.blocksize;

    if (size() < off_ + n)
        resize(off_ + n);

    convert2fc32(buffer, n, data() + off_);

    off_ += n;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void FLACMemoryDecoder::metadata_callback(const FLAC__StreamMetadata *metadata)
{
}

void FLACMemoryDecoder::error_callback(FLAC__StreamDecoderErrorStatus status)
{
    throw FLACException(FLAC__StreamDecoderErrorStatusString[status]);
}
