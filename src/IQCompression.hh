// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef IQCOMPRESSION_H_
#define IQCOMPRESSION_H_

#include <atomic>
#include <complex>
#include <deque>
#include <memory>
#include <vector>

#include <FLAC++/decoder.h>
#include <FLAC++/encoder.h>

#include "IQBuffer.hh"

typedef std::complex<int16_t> sc16_t;
typedef std::complex<float> fc32_t;
typedef std::complex<double> fc64_t;

/** @brief Convert fc32 format to sc16 */
void convert2sc16(const fc32_t *from, sc16_t *to, size_t n);

/** @brief Convert sc16 format to fc32 */
void convert2fc32(const sc16_t *from, fc32_t *to, size_t n);

/** @brief Compress fc32 data with FLAC */
buffer<char> compressFLAC(unsigned compression_level, const fc32_t *data, size_t n);

/** @brief Decompress FLAC-encoded fc32 data */
buffer<fc32_t> decompressFLAC(const char *data, size_t n);

#endif /* IQCOMPRESSION_H_ */
