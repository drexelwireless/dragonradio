// C ABI for the native frame engine, consumed by simulation/fast.py via ctypes.
// The six v1 functions keep their names, argument order and return codes; v2
// adds configurable modulation/Chase, a persistent stream and a version query.
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "FastPHYCore.hh"

using dragonradio::native::C;
using dragonradio::native::FastPHY;
using dragonradio::native::Stream;

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
}
