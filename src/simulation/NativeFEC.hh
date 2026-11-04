// Coding for the native PHY (phase P3): a port of liquid-dsp's packetizer
// (CRC, whitening, two FEC/interleaver stages) so that a native coded payload
// carries exactly the bit stream liquid's packetizer produces for the same
// MCS. The Reed-Solomon and Viterbi cores are libcorrect through its libfec
// API, the same functions liquid calls when built against libcorrect's shim
// (as build.sh and grid/setup-fullstack-build.sh do).
//
// Ported from liquid-dsp (src/fec/src/{packetizer,interleaver,fec_rs,fec_conv,
// fec_conv_punctured,fec_conv_pmatrix,crc}.c, src/random/src/scramble.c),
// Copyright (c) 2007 - 2015 Joseph Gaeddert, MIT license:
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit
//   persons to whom the Software is furnished to do so, subject to the
//   following conditions: The above copyright notice and this permission
//   notice shall be included in all copies or substantial portions of the
//   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
//
// Parity with liquid is tested bit-for-bit (tests/fullstack/test_native_fec.cc),
// including liquid's quirks: the last Reed-Solomon block of a message is
// padded with stale bytes of the previous block, convolutional tails are
// zero-flushed and byte-padded, and a stage without FEC is not interleaved.
//
// Compiled only with DRAGONRADIO_NATIVE_FEC (links libfec, i.e. libcorrect's
// shim). Without it, coded schemes are rejected at run time.

#ifndef DRAGONRADIO_SIMULATION_NATIVEFEC_HH_
#define DRAGONRADIO_SIMULATION_NATIVEFEC_HH_

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(DRAGONRADIO_NATIVE_FEC)
extern "C" {
#include <fec.h>
}
#endif

namespace dragonradio::native::fec {

// Native code identifiers carried in the v3 frame header. Values are part of
// the wire format.
enum Code : uint8_t {
    kNone = 0,
    kRS8 = 1,      // Reed-Solomon (255,223), m = 8
    kV27 = 2,      // r1/2 K=7
    kV29 = 3,      // r1/2 K=9
    kV27P23 = 4, kV27P34, kV27P45, kV27P56, kV27P67, kV27P78,
    kV29P23 = 10, kV29P34, kV29P45, kV29P56, kV29P67, kV29P78,
    kNumCodes = 16
};

inline const char *name(Code c)
{
    static const char *names[kNumCodes] = {
        "none", "rs8", "v27", "v29",
        "v27p23", "v27p34", "v27p45", "v27p56", "v27p67", "v27p78",
        "v29p23", "v29p34", "v29p45", "v29p56", "v29p67", "v29p78"};
    return c < kNumCodes ? names[c] : "invalid";
}

inline bool valid(int c) { return c >= 0 && c < kNumCodes; }

#if defined(DRAGONRADIO_NATIVE_FEC)
constexpr bool kAvailable = true;
#else
constexpr bool kAvailable = false;
#endif

// liquid soft-bit convention
constexpr unsigned char kSoft0 = 0, kSoft1 = 255, kSoftErasure = 127;

namespace detail {

inline bool conv(Code c) { return c >= kV27 && c <= kV29P78; }
inline bool punctured(Code c) { return c >= kV27P23; }
inline int constraint(Code c) { return (c == kV27 || (c >= kV27P23 && c <= kV27P78)) ? 7 : 9; }
// Puncturing period P: p23 -> 2 ... p78 -> 7
inline int period(Code c) { return punctured(c) ? 2 + (c - (constraint(c) == 7 ? kV27P23 : kV29P23)) : 1; }

// liquid fec_conv_pmatrix.c (2 rows x P columns)
inline const int *pmatrix(Code c)
{
    static const int m27[6][14] = {
        {1, 1, 1, 0},
        {1, 1, 0, 1, 0, 1},
        {1, 1, 1, 1, 1, 0, 0, 0},
        {1, 1, 0, 1, 0, 1, 0, 1, 0, 1},
        {1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
        {1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1}};
    static const int m29[6][14] = {
        {1, 1, 1, 0},
        {1, 1, 1, 1, 0, 0},
        {1, 1, 0, 1, 1, 0, 1, 0},
        {1, 0, 1, 1, 0, 1, 1, 0, 0, 1},
        {1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1},
        {1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 0}};
    if (!punctured(c))
        return nullptr;
    return constraint(c) == 7 ? m27[c - kV27P23] : m29[c - kV29P23];
}

inline unsigned enc_len(Code c, unsigned n)
{
    switch (c) {
    case kNone: return n;
    case kRS8: {
        const unsigned blocks = (n + 222) / 223;
        const unsigned dec_block = (n + blocks - 1) / blocks;
        return (dec_block + 32) * blocks;
    }
    case kV27:
    case kV29: return 2 * n + 2;
    default: {
        const unsigned bits = 8 * n + constraint(c) - 1;
        const unsigned p = period(c);
        const unsigned out = bits + (bits + p - 1) / p;
        return (out + 7) / 8;
    }
    }
}

inline uint32_t crc32(const unsigned char *p, size_t n)
{
    uint32_t key = ~0u;
    for (size_t i = 0; i < n; ++i) {
        key ^= p[i];
        for (int j = 0; j < 8; ++j)
            key = (key >> 1) ^ (0xEDB88320u & (0u - (key & 1)));
    }
    return ~key;
}

inline void scramble(unsigned char *x, size_t n)
{
    static const unsigned char mask[4] = {0xb4, 0x6a, 0x8b, 0xc5};
    for (size_t i = 0; i < n; ++i)
        x[i] ^= mask[i & 3];
}

inline int pc(unsigned x)
{
    return __builtin_parity(x);
}

// liquid interleaver (bytes, or 8 soft bits per byte when soft)
struct Interleaver {
    unsigned n = 0, M = 0, N = 0, depth = 4;

    Interleaver() = default;
    explicit Interleaver(unsigned n_) : n(n_)
    {
        M = 1 + static_cast<unsigned>(std::floor(std::sqrt(static_cast<float>(n))));
        N = n / M;
        while (n >= M * N) N++;
    }

    template <class F>
    void walk(unsigned Ncol, F swap) const
    {
        unsigned m = 0, col = n / 3, n2 = n / 2, j;
        for (unsigned i = 0; i < n2; ++i) {
            do {
                j = m * Ncol + col;
                if (++m == M) { col = (col + 1) % Ncol; m = 0; }
            } while (j >= n2);
            swap(2 * i, 2 * j + 1);
        }
    }

    void permute(unsigned char *x, unsigned Ncol, unsigned char mask, bool soft) const
    {
        walk(Ncol, [&](unsigned a, unsigned b) {
            if (!soft) {
                if (mask == 0xff) {
                    std::swap(x[a], x[b]);
                } else {
                    const unsigned char t0 = (x[a] & ~mask) | (x[b] & mask);
                    const unsigned char t1 = (x[a] & mask) | (x[b] & ~mask);
                    x[a] = t0;
                    x[b] = t1;
                }
            } else {
                for (int k = 0; k < 8; ++k)
                    if ((mask >> (7 - k)) & 1)
                        std::swap(x[8 * b + k], x[8 * a + k]);
            }
        });
    }

    void encode(unsigned char *x, bool soft = false) const
    {
        if (depth > 0) permute(x, N, 0xff, soft);
        if (depth > 1) permute(x, N + 2, 0x0f, soft);
        if (depth > 2) permute(x, N + 4, 0x55, soft);
        if (depth > 3) permute(x, N + 8, 0x33, soft);
    }

    void decode(unsigned char *x, bool soft = false) const
    {
        if (depth > 3) permute(x, N + 8, 0x33, soft);
        if (depth > 2) permute(x, N + 4, 0x55, soft);
        if (depth > 1) permute(x, N + 2, 0x0f, soft);
        if (depth > 0) permute(x, N, 0xff, soft);
    }
};

#if defined(DRAGONRADIO_NATIVE_FEC)
// One FEC stage for a fixed decoded length.
struct Stage {
    Code code;
    unsigned dec_len, enc_len_;
    // Reed-Solomon
    void *rs = nullptr;
    unsigned blocks = 0, dec_block = 0, enc_block = 0, res = 0;
    std::vector<unsigned char> tblock;
    std::vector<int> derrlocs;
    // convolutional
    void *vp = nullptr;
    std::vector<unsigned char> enc_bits;

    Stage(Code c, unsigned n) : code(c), dec_len(n), enc_len_(enc_len(c, n))
    {
        if (c == kRS8) {
            blocks = (n + 222) / 223;
            dec_block = (n + blocks - 1) / blocks;
            enc_block = dec_block + 32;
            res = (blocks * dec_block) % n;
            tblock.assign(255, 0);
            derrlocs.assign(255, 0);
            rs = init_rs_char(8, 0x11d, 1, 1, 32, 223 - dec_block);
            if (!rs)
                throw std::runtime_error("init_rs_char failed");
        } else if (conv(c)) {
            const int K = constraint(c);
            vp = K == 7 ? create_viterbi27(8 * n) : create_viterbi29(8 * n);
            enc_bits.resize(2 * (8 * n + K - 1));
        }
    }

    Stage(const Stage &) = delete;
    Stage &operator=(const Stage &) = delete;

    ~Stage()
    {
        if (rs) free_rs_char(rs);
        if (vp) (constraint(code) == 7 ? delete_viterbi27 : delete_viterbi29)(vp);
    }

    void encode(const unsigned char *in, unsigned char *out)
    {
        if (code == kNone) {
            std::memmove(out, in, dec_len);
        } else if (code == kRS8) {
            unsigned n0 = 0, n1 = 0, size = dec_block;
            for (unsigned i = 0; i < blocks; ++i) {
                if (i == blocks - 1)
                    size -= res;
                // liquid copies only 'size' bytes: the residual tail of the
                // last block keeps the previous block's bytes.
                std::memmove(tblock.data(), in + n0, size);
                encode_rs_char(rs, tblock.data(), tblock.data() + dec_block);
                std::memmove(out + n1, tblock.data(), enc_block);
                n0 += size;
                n1 += enc_block;
            }
        } else {
            const int K = constraint(code);
            const unsigned poly[2] = {static_cast<unsigned>(K == 7 ? V27POLYA : V29POLYA),
                                      static_cast<unsigned>(K == 7 ? V27POLYB : V29POLYB)};
            const int *pm = pmatrix(code);
            const unsigned P = period(code);
            unsigned sr = 0, n = 0, p = 0;
            unsigned char byte = 0;
            auto emit = [&](unsigned r) {
                if (pm && !pm[r * P + p])
                    return;
                byte = static_cast<unsigned char>((byte << 1) | pc(sr & poly[r]));
                out[n / 8] = byte;
                n++;
            };
            for (unsigned i = 0; i < dec_len; ++i)
                for (int j = 0; j < 8; ++j) {
                    sr = (sr << 1) | ((in[i] >> (7 - j)) & 1);
                    emit(0); emit(1);
                    p = (p + 1) % P;
                }
            for (int i = 0; i < K - 1; ++i) {
                sr <<= 1;
                emit(0); emit(1);
                p = (p + 1) % P;
            }
            while (n % 8) {
                byte <<= 1;
                out[n / 8] = byte;
                n++;
            }
        }
    }

    // Hard decision.
    void decode(const unsigned char *in, unsigned char *out)
    {
        if (code == kNone) {
            std::memmove(out, in, dec_len);
        } else if (code == kRS8) {
            unsigned n0 = 0, n1 = 0, size = dec_block;
            for (unsigned i = 0; i < blocks; ++i) {
                if (i == blocks - 1)
                    size -= res;
                std::memmove(tblock.data(), in + n0, enc_block);
                decode_rs_char(rs, tblock.data(), derrlocs.data(), 0);
                std::memmove(out + n1, tblock.data(), size);
                n0 += enc_block;
                n1 += size;
            }
        } else {
            // Unpack to soft bits, then decode as soft input.
            std::vector<unsigned char> soft(8 * enc_len_);
            for (unsigned i = 0; i < 8 * enc_len_; ++i)
                soft[i] = ((in[i / 8] >> (7 - i % 8)) & 1) ? kSoft1 : kSoft0;
            decode_soft(soft.data(), out);
        }
    }

    // Soft input: 8 soft bits (0..255) per encoded byte.
    void decode_soft(const unsigned char *in, unsigned char *out)
    {
        if (!conv(code)) {
            // liquid packs soft bits to hard and uses hard decoding
            std::vector<unsigned char> hard(enc_len_);
            for (unsigned i = 0; i < enc_len_; ++i) {
                unsigned char b = 0;
                for (int k = 0; k < 8; ++k)
                    b |= static_cast<unsigned char>(((in[8 * i + k] >> 7) & 1) << (7 - k));
                hard[i] = b;
            }
            decode(hard.data(), out);
            return;
        }
        const int K = constraint(code);
        const int *pm = pmatrix(code);
        const unsigned P = period(code);
        const unsigned groups = 8 * dec_len + K - 1;
        if (!pm) {
            std::memcpy(enc_bits.data(), in, 2 * groups);
        } else {
            unsigned n = 0, p = 0;
            for (unsigned i = 0; i < 2 * groups; i += 2) {
                for (unsigned r = 0; r < 2; ++r)
                    enc_bits[i + r] = pm[r * P + p] ? in[n++] : kSoftErasure;
                p = (p + 1) % P;
            }
        }
        if (K == 7) {
            init_viterbi27(vp, 0);
            update_viterbi27_blk(vp, enc_bits.data(), groups);
            chainback_viterbi27(vp, out, 8 * dec_len, 0);
        } else {
            init_viterbi29(vp, 0);
            update_viterbi29_blk(vp, enc_bits.data(), groups);
            chainback_viterbi29(vp, out, 8 * dec_len, 0);
        }
    }
};
#endif

}  // namespace detail

// Encoded payload length (bytes) for msg_len bytes with CRC-32 and stages
// fec0 then fec1, as liquid's packetizer_compute_enc_msg_len.
inline unsigned encoded_length(unsigned msg_len, Code fec0, Code fec1)
{
    return detail::enc_len(fec1, detail::enc_len(fec0, msg_len + 4));
}

// liquid-compatible packetizer with CRC-32, for one message length.
class Packetizer {
public:
    Packetizer(unsigned msg_len, Code fec0, Code fec1)
      : msg_len_(msg_len), fec_{fec0, fec1}
    {
        if (!valid(fec0) || !valid(fec1))
            throw std::invalid_argument("invalid FEC code");
#if !defined(DRAGONRADIO_NATIVE_FEC)
        if (fec0 != kNone || fec1 != kNone)
            throw std::runtime_error("native FEC not built (DRAGONRADIO_NATIVE_FEC)");
#endif
        unsigned n = msg_len + 4;
        for (int i = 0; i < 2; ++i) {
            dec_[i] = n;
            enc_[i] = detail::enc_len(fec_[i], n);
            intlv_[i] = detail::Interleaver(enc_[i]);
            if (fec_[i] == kNone)
                intlv_[i].depth = 0;
#if defined(DRAGONRADIO_NATIVE_FEC)
            stage_[i] = std::make_unique<detail::Stage>(fec_[i], n);
#endif
            n = enc_[i];
        }
        packet_len_ = n;
        unsigned m = std::max(packet_len_, msg_len + 4);
        b0_.resize(8 * m);
        b1_.resize(8 * m);
    }

    unsigned message_length() const { return msg_len_; }
    unsigned packet_length() const { return packet_len_; }

    void encode(const unsigned char *msg, unsigned char *pkt)
    {
        std::memmove(b0_.data(), msg, msg_len_);
        const uint32_t key = detail::crc32(b0_.data(), msg_len_);
        for (int i = 0; i < 4; ++i)
            b0_[msg_len_ + 3 - i] = static_cast<unsigned char>(key >> (8 * i));
        detail::scramble(b0_.data(), msg_len_ + 4);
        for (int i = 0; i < 2; ++i) {
#if defined(DRAGONRADIO_NATIVE_FEC)
            stage_[i]->encode(b0_.data(), b1_.data());
#else
            std::memmove(b1_.data(), b0_.data(), dec_[i]);
#endif
            std::memmove(b0_.data(), b1_.data(), enc_[i]);
            intlv_[i].encode(b0_.data());
        }
        std::memmove(pkt, b0_.data(), packet_len_);
    }

    // Hard decision. Returns CRC validity.
    bool decode(const unsigned char *pkt, unsigned char *msg)
    {
        std::memmove(b0_.data(), pkt, packet_len_);
        for (int i = 1; i >= 0; --i) {
            std::memmove(b1_.data(), b0_.data(), enc_[i]);
            intlv_[i].decode(b1_.data());
#if defined(DRAGONRADIO_NATIVE_FEC)
            stage_[i]->decode(b1_.data(), b0_.data());
#else
            std::memmove(b0_.data(), b1_.data(), dec_[i]);
#endif
        }
        return finish(msg);
    }

    // Soft input (8 soft bits per packet byte, liquid convention): the outer
    // stage (fec1) is decoded from soft bits, the inner (fec0) from hard
    // decisions, as liquid's packetizer_decode_soft.
    bool decode_soft(const unsigned char *soft, unsigned char *msg)
    {
        std::memmove(b0_.data(), soft, 8 * packet_len_);
        std::memmove(b1_.data(), b0_.data(), 8 * enc_[1]);
        intlv_[1].decode(b1_.data(), true);
#if defined(DRAGONRADIO_NATIVE_FEC)
        stage_[1]->decode_soft(b1_.data(), b0_.data());
#else
        for (unsigned i = 0; i < dec_[1]; ++i) {
            unsigned char b = 0;
            for (int k = 0; k < 8; ++k)
                b |= static_cast<unsigned char>(((b1_[8 * i + k] >> 7) & 1) << (7 - k));
            b0_[i] = b;
        }
#endif
        std::memmove(b1_.data(), b0_.data(), enc_[0]);
        intlv_[0].decode(b1_.data());
#if defined(DRAGONRADIO_NATIVE_FEC)
        stage_[0]->decode(b1_.data(), b0_.data());
#else
        std::memmove(b0_.data(), b1_.data(), dec_[0]);
#endif
        return finish(msg);
    }

private:
    unsigned msg_len_, packet_len_ = 0;
    Code fec_[2];
    unsigned dec_[2] = {0, 0}, enc_[2] = {0, 0};
    detail::Interleaver intlv_[2];
#if defined(DRAGONRADIO_NATIVE_FEC)
    std::unique_ptr<detail::Stage> stage_[2];
#endif
    std::vector<unsigned char> b0_, b1_;

    bool finish(unsigned char *msg)
    {
        detail::scramble(b0_.data(), msg_len_ + 4);
        uint32_t key = 0;
        for (int i = 0; i < 4; ++i)
            key = (key << 8) | b0_[msg_len_ + i];
        std::memmove(msg, b0_.data(), msg_len_);
        return detail::crc32(b0_.data(), msg_len_) == key;
    }
};

// Packetizers cached by (length, fec0, fec1); one cache per thread/object.
class PacketizerCache {
public:
    Packetizer &get(unsigned msg_len, Code fec0, Code fec1)
    {
        auto key = std::make_tuple(msg_len, static_cast<int>(fec0), static_cast<int>(fec1));
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            if (cache_.size() > 64)
                cache_.clear();
            it = cache_.emplace(key, std::make_unique<Packetizer>(msg_len, fec0, fec1)).first;
        }
        return *it->second;
    }

private:
    std::map<std::tuple<unsigned, int, int>, std::unique_ptr<Packetizer>> cache_;
};

}  // namespace dragonradio::native::fec

#endif  // DRAGONRADIO_SIMULATION_NATIVEFEC_HH_
