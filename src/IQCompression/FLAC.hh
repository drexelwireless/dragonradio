#ifndef IQCOMPRESSION_FLAC_H_
#define IQCOMPRESSION_FLAC_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <vector>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

class FLACException : public std::exception {
public:
    FLACException(const char *msg) : msg_(msg)
    {
    }

    virtual ~FLACException() = default;

	const char *what() const throw()
    {
    	return msg_;
    }

protected:
    /** @brief The FLAC error message */
    const char *msg_;
};

class FLACMemoryEncoder : public FLAC::Encoder::Stream {
public:
    FLACMemoryEncoder();
    virtual ~FLACMemoryEncoder() = default;

    virtual void encode(unsigned compression_level,
                        const fc32_t *sig,
                        size_t n);

protected:
    /** @brief Offset into buffer at which to write data */
    size_t off_;

    /** @brief Get size of buffer holding encoded */
    virtual size_t size(void) = 0;

    /** @brief Get pointer to buffer holding encoded */
    virtual char *data(void) = 0;

    /** @brief Resize buffer holding encoded data */
    virtual void resize(size_t size) = 0;

    FLAC__StreamEncoderReadStatus read_callback(FLAC__byte buffer[], size_t *bytes) override;

    FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame) override;

    FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset) override;

    FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *absolute_byte_offset) override;

    void metadata_callback(const FLAC__StreamMetadata *metadata) override;
};

class FLACMemoryDecoder : public FLAC::Decoder::Stream {
public:
    FLACMemoryDecoder(const char *encoded, size_t n);
    virtual ~FLACMemoryDecoder() = default;

    virtual void decode(void);

protected:
    /** @brief Buffer holding encoded data */
    const char *encoded_;

    /** @brief Size of encoded data */
    size_t encoded_size_;

    /** @brief Offset into buffer holding encoded data */
    size_t encoded_off_;

    /** @brief Offset into buffer holding decoded data */
    size_t off_;

    /** @brief Get size of buffer holding decoded data */
    virtual size_t size(void) = 0;

    /** @brief Get pointer to buffer holding decoded data */
    virtual float *data(void) = 0;

    /** @brief Resize buffer holding decoded data */
    virtual void resize(size_t size) = 0;

    FLAC__StreamDecoderReadStatus read_callback(FLAC__byte buffer[], size_t *bytes) override;

    FLAC__StreamDecoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset) override;

    FLAC__StreamDecoderTellStatus tell_callback(FLAC__uint64 *absolute_byte_offset) override;

    FLAC__StreamDecoderLengthStatus length_callback(FLAC__uint64 *stream_length) override;

    bool eof_callback() override;

    FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame *frame, const FLAC__int32 *const buffer[]) override;

    void metadata_callback(const FLAC__StreamMetadata *metadata) override;

    void error_callback(FLAC__StreamDecoderErrorStatus status) override;
};

#endif /* IQCOMPRESSION_FLAC_H_ */
