// C ABI for the native frame engine, consumed by simulation/fast.py via ctypes.
// The six v1 functions keep their names, argument order and return codes; v2
// adds configurable modulation/Chase, a persistent stream and a version query.
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "FastPHYCore.hh"
#include "NativeFrame.hh"

using dragonradio::native::C;
using dragonradio::native::FastPHY;
using dragonradio::native::Stream;
using dragonradio::native::FramedPHY;
using dragonradio::native::FramedStream;
using dragonradio::native::FrameHeader;

static thread_local std::string fast_error;

struct StreamHandle {
    Stream stream;
    std::vector<Stream::Result> results;
    size_t delivered = 0;
    StreamHandle(FastPHY &phy, int user) : stream(phy, user) {}
};

extern "C" {
const char *dr_fast_error() { return fast_error.c_str(); }
int dr_fast_version() { return 2; }
void *dr_fast_create(int n, int cp, int ns, int nu, const int *ids, const int *nd, const int *db,
                     const int *np, const int *pb, const C *tr, const unsigned char *prbs) {
    try { return new FastPHY(n, cp, ns, nu, ids, nd, db, np, pb, tr, prbs); }
    catch (const std::exception &e) { fast_error = e.what(); return nullptr; }
}
void *dr_fast_create2(int n, int cp, int ns, int nu, const int *ids, const int *nd, const int *db,
                      const int *np, const int *pb, const C *tr, const unsigned char *prbs,
                      int bits_per_symbol, int chase_bits) {
    try { return new FastPHY(n, cp, ns, nu, ids, nd, db, np, pb, tr, prbs, bits_per_symbol, chase_bits); }
    catch (const std::exception &e) { fast_error = e.what(); return nullptr; }
}
void dr_fast_destroy(void *p) { delete static_cast<FastPHY *>(p); }
int dr_fast_channel_filter(void *p, int ntaps, int count, const int *bins, const C *fit, const C *basis) {
    try { static_cast<FastPHY *>(p)->add_channel_model(ntaps, count, bins, fit, basis); return 0; }
    catch (const std::exception &e) { fast_error = e.what(); return -1; }
}
int dr_fast_write(void *p, const unsigned char *bytes, const int *sizes, C *out, int capacity) {
    try { return static_cast<FastPHY *>(p)->write_checked(bytes, sizes, out, capacity); }
    catch (const std::exception &e) { fast_error = e.what(); return -1; }
}
int dr_fast_receive(void *p, const C *in, int count, int user, unsigned char *out, int capacity, double *metrics) {
    try { return static_cast<FastPHY *>(p)->receive(in, count, user, out, capacity, metrics); }
    catch (const std::exception &e) { fast_error = e.what(); return -4; }
}
// Streams borrow the engine; destroy every stream before its engine.
void *dr_fast_stream_create(void *p, int user) {
    try { return new StreamHandle(*static_cast<FastPHY *>(p), user); }
    catch (const std::exception &e) { fast_error = e.what(); return nullptr; }
}
void dr_fast_stream_destroy(void *s) { delete static_cast<StreamHandle *>(s); }
void dr_fast_stream_reset(void *s) {
    auto &h = *static_cast<StreamHandle *>(s);
    h.stream.reset(); h.results.clear(); h.delivered = 0;
}
long long dr_fast_stream_pending(void *s) { return static_cast<StreamHandle *>(s)->stream.pending(); }
// Feed samples; returns the number of decode attempts now queued (>= 0) or -4.
int dr_fast_stream_feed(void *s, const C *in, int count, int final) {
    try {
        auto &h = *static_cast<StreamHandle *>(s);
        if (h.delivered == h.results.size()) { h.results.clear(); h.delivered = 0; }
        h.stream.feed(in, count, final != 0, h.results);
        return int(h.results.size() - h.delivered);
    } catch (const std::exception &e) { fast_error = e.what(); return -4; }
}
// Pop the next queued attempt: copies its bytes (up to capacity) and six
// metrics [absolute start, sync, cfo, evm, chase trials, status]. Returns the
// byte count, a negative RX_* status for a failed attempt, or -5 if empty.
int dr_fast_stream_next(void *s, unsigned char *out, int capacity, double *metrics) {
    auto &h = *static_cast<StreamHandle *>(s);
    if (h.delivered >= h.results.size()) return -5;
    const auto &r = h.results[h.delivered++];
    for (int i = 0; i < 5; ++i) metrics[i] = r.metrics[i];
    metrics[0] = double(r.start);
    metrics[5] = r.status;
    if (r.status < 0) return r.status;
    int count = std::min<int>(capacity, int(r.bytes.size()));
    std::copy(r.bytes.begin(), r.bytes.begin() + count, out);
    return count;
}

// ---- Framed native OFDM (wire format v2, see NativeFrame.hh) --------------
struct FramedStreamHandle {
    FramedStream stream;
    std::vector<FramedStream::Result> results;
    size_t delivered = 0;
    FramedStreamHandle(FramedPHY &phy, FramedStream::want_type want) : stream(phy, std::move(want)) {}
};
void *dr_frame_create(int n, int cp, int nd, const int *data, int np, const int *pilots, int max_payload, int chase, int user) {
    try { return new FramedPHY(n, cp, std::vector<int>(data, data + nd), std::vector<int>(pilots, pilots + np), max_payload, chase, user); }
    catch (const std::exception &e) { fast_error = e.what(); return nullptr; }
}
void dr_frame_destroy(void *p) { delete static_cast<FramedPHY *>(p); }
int dr_frame_samples(void *p, int length, int bps) {
    try {
        if (bps != 2 && bps != 4) throw std::runtime_error("unsupported constellation");
        return static_cast<FramedPHY *>(p)->frame_samples(length, bps);
    } catch (const std::exception &e) { fast_error = e.what(); return -1; }
}
int dr_frame_header_symbols(void *p) { return static_cast<FramedPHY *>(p)->header_symbols; }
int dr_frame_write(void *p, const unsigned char *info, int mcsidx, int bps, const unsigned char *payload, int length, C *out, int capacity) {
    try { return static_cast<FramedPHY *>(p)->write(info, mcsidx, bps, payload, length, out, capacity); }
    catch (const std::exception &e) { fast_error = e.what(); return -1; }
}
// nexthop_filter < 0 accepts every valid header; otherwise only headers whose
// next-hop byte (info[1]) equals it or broadcast 255 (exercises the skip path).
void *dr_frame_stream_create(void *p, int nexthop_filter) {
    try {
        FramedStream::want_type want;
        if (nexthop_filter >= 0)
            want = [nexthop_filter](const FrameHeader &h) { return h.info[1] == nexthop_filter || h.info[1] == 255; };
        return new FramedStreamHandle(*static_cast<FramedPHY *>(p), want);
    } catch (const std::exception &e) { fast_error = e.what(); return nullptr; }
}
void dr_frame_stream_destroy(void *s) { delete static_cast<FramedStreamHandle *>(s); }
void dr_frame_stream_reset(void *s) {
    auto &h = *static_cast<FramedStreamHandle *>(s);
    h.stream.reset(); h.results.clear(); h.delivered = 0;
}
long long dr_frame_stream_pending(void *s) { return static_cast<FramedStreamHandle *>(s)->stream.pending_samples(); }
int dr_frame_stream_open(void *s) { return static_cast<FramedStreamHandle *>(s)->stream.frame_open(); }
int dr_frame_stream_feed(void *s, const C *in, int count, int final) {
    try {
        auto &h = *static_cast<FramedStreamHandle *>(s);
        if (h.delivered == h.results.size()) { h.results.clear(); h.delivered = 0; }
        h.stream.feed(in, count, final != 0, h.results);
        return int(h.results.size() - h.delivered);
    } catch (const std::exception &e) { fast_error = e.what(); return -4; }
}
// meta[13]: start, samples, header_valid, wanted, payload_valid, mcsidx, bps,
// length, sync, cfo, evm, rssi_db, chase_trials. info receives 5 header bytes.
// Returns the payload byte count copied, or -5 when no result is queued.
int dr_frame_stream_next(void *s, unsigned char *info, unsigned char *payload, int capacity, double *meta) {
    auto &h = *static_cast<FramedStreamHandle *>(s);
    if (h.delivered >= h.results.size()) return -5;
    const auto &r = h.results[h.delivered++];
    const double values[13] = {double(r.start), double(r.samples), double(r.header_valid), double(r.wanted),
                               double(r.payload_valid), double(r.header.mcsidx), double(r.header.bps),
                               double(r.header.length), r.sync, r.cfo, r.evm, r.rssi_db, r.chase_trials};
    std::copy(values, values + 13, meta);
    std::memcpy(info, r.header.info, 5);
    int count = std::min<int>(capacity, int(r.payload.size()));
    std::copy(r.payload.begin(), r.payload.begin() + count, payload);
    return count;
}
}

