// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "IQCompression.hh"
#include "IQCompression/FLAC.hh"

inline void checkFLACBool(FLAC__bool flag)
{
    if (!flag)
        throw FLACException("FLAC encoder/decoder already initialized");
}

FLACMemoryEncoder::FLACMemoryEncoder() : off_(0)
{
}

void FLACMemoryEncoder::encode(unsigned compression_level,
                               const fc32_t *sig,
                               size_t n)
{
    checkFLACBool(is_valid());
    checkFLACBool(set_verify(true));
    checkFLACBool(set_compression_level(compression_level));
    checkFLACBool(set_channels(2));
    checkFLACBool(set_bits_per_sample(16));
    // We can't set the sampel rate here or we get an error
    //checkFLACBool(set_sample_rate(fs));
    checkFLACBool(set_total_samples_estimate(n));

    FLAC__StreamEncoderInitStatus init_status = init();

    if(init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
		throw FLACException(FLAC__StreamEncoderInitStatusString[init_status]);

    std::unique_ptr<int32_t[]> tempbuf = std::unique_ptr<int32_t[]>(new int32_t[2*n]);
    int32_t                    *temp = tempbuf.get();
    constexpr float            k = 32767.;

    for (unsigned i = 0; i < n; ++i) {
        temp[2*i] = k*sig[i].real();
        temp[2*i+1] = k*sig[i].imag();
    }

    process_interleaved(temp, n);

    finish();
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
    return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;
}

FLAC__StreamEncoderTellStatus FLACMemoryEncoder::tell_callback(FLAC__uint64 *absolute_byte_offset)
{
    return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;
}

void FLACMemoryEncoder::metadata_callback(const FLAC__StreamMetadata *metadata)
{
}

FLACMemoryDecoder::FLACMemoryDecoder(const char *encoded, size_t n)
    : encoded_(encoded)
    , encoded_size_(n)
    , encoded_off_(0)
    , off_(0)
{
}

void FLACMemoryDecoder::decode(void)
{
    FLAC__StreamDecoderInitStatus init_status = init();

    if(init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
		throw FLACException(FLAC__StreamDecoderInitStatusString[init_status]);

    process_until_end_of_stream();
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
    constexpr float k = 1/32767.;

    if (size() < off_ + n)
        resize(off_ + n);

    float *x = data();

    for (size_t i = 0; i < n; ++i) {
        x[2*(off_+i)] = k*buffer[0][i];
        x[2*(off_+i)+1] = k*buffer[1][i];
    }

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
