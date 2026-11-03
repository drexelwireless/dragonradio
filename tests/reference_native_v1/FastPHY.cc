// C ABI for the native frame engine, consumed by simulation/fast.py via ctypes.
// Symbol names, argument order and return codes are unchanged from the original
// single-file implementation; the algorithm lives in FastPHYCore.hh.
#include <exception>
#include <string>

#include "FastPHYCore.hh"

using dragonradio::native::C;
using dragonradio::native::FastPHY;

static thread_local std::string fast_error;
extern "C" {
const char *dr_fast_error() { return fast_error.c_str(); }
void *dr_fast_create(int n, int cp, int ns, int nu, const int *ids, const int *nd, const int *db,
                     const int *np, const int *pb, const C *tr, const unsigned char *prbs) {
    try { return new FastPHY(n, cp, ns, nu, ids, nd, db, np, pb, tr, prbs); }
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
}
