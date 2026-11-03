// Framed native OFDM: a variable-length single-allocation frame carrying a
// DragonRadio packet header, built on FastPHYCore.hh acquisition, channel
// estimation and demapping. FFTW and the C++17 runtime only (no liquid, UHD).
//
// Wire format "native frame v2" (format byte 0xD2):
//   2 training symbols (identical, Schmidl-Cox detectable)
//   H header symbols, always QPSK, carrying 14 bytes:
//       [0]      format byte 0xD2
//       [1..5]   DragonRadio Header (curhop, nexthop, seq, flags), host order
//       [6]      MCS index (into the transmitter's table)
//       [7]      payload bits per symbol: 2 (QPSK) or 4 (Gray 16-QAM)
//       [8..9]   payload length in bytes, big endian
//       [10..13] CRC-32 of bytes 0..9, big endian
//   P payload symbols at the announced constellation carrying
//       payload bytes followed by CRC-32 of the payload, big endian
// H = ceil(112 / (2 * data tones)); P = ceil((len + 4) * 8 / (bps * data tones)).
// Header and payload bits are scrambled by one per-user PRBS-15 sequence
// (header first). Training is +/-1 on every active tone from PRBS-15. Pilots
// alternate sign with data-symbol index as in the fixed-frame engine.
#ifndef DRAGONRADIO_SIMULATION_NATIVEFRAME_HH_
#define DRAGONRADIO_SIMULATION_NATIVEFRAME_HH_

#include <functional>
#include <optional>

#include "FastPHYCore.hh"

namespace dragonradio::native {

constexpr unsigned char kFrameFormat = 0xD2;
constexpr int kHeaderBytes = 14;
constexpr int kHeaderInfoBytes = 5;   // sizeof(Header) in the production stack

// PRBS-15 (x^15 + x^14 + 1), Fibonacci form, one bit per call.
inline std::vector<unsigned char> prbs15(uint32_t seed, size_t count) {
    uint32_t state = (seed & 0x7fff) ? (seed & 0x7fff) : 1;
    std::vector<unsigned char> bits(count);
    for (size_t i = 0; i < count; ++i) {
        unsigned b = ((state >> 14) ^ (state >> 13)) & 1;
        state = ((state << 1) | b) & 0x7fff;
        bits[i] = (unsigned char)b;
    }
    return bits;
}

struct FrameHeader {
    bool valid = false;
    unsigned char info[kHeaderInfoBytes] = {0, 0, 0, 0, 0};
    int mcsidx = 0, bps = 2, length = 0;
    int header_symbols = 0, payload_symbols = 0, samples = 0;
};

struct FramedPHY {
    int n, cp, nd, max_payload, chase, header_symbols;
    std::vector<int> data_bins, pilot_bins;
    std::unique_ptr<FastPHY> core;
    int user_id;

    // bins are FFT indices in [0, n); one allocation (full-band OFDM).
    FramedPHY(int n_, int cp_, const std::vector<int> &data, const std::vector<int> &pilots,
              int max_payload_, int chase_bits = 4, int user = 1)
      : n(n_), cp(cp_), nd(int(data.size())), max_payload(max_payload_), chase(chase_bits),
        header_symbols(0), data_bins(data), pilot_bins(pilots), user_id(user) {
        if (nd < 8 || pilot_bins.size() < 2) throw std::runtime_error("allocation needs >= 8 data and >= 2 pilot tones");
        if (max_payload < 0 || max_payload > 65535) throw std::runtime_error("invalid maximum payload");
        header_symbols = (kHeaderBytes * 8 + 2 * nd - 1) / (2 * nd);
        // Worst case is QPSK: enough symbols for header plus the largest payload.
        const int payload_symbols = ((max_payload + 4) * 8 + 2 * nd - 1) / (2 * nd);
        const int ns = header_symbols + payload_symbols;
        // Training: +/-1 on active tones in ascending signed-frequency order.
        std::vector<int> active(data);
        active.insert(active.end(), pilots.begin(), pilots.end());
        auto signed_bin = [&](int k) { return k >= n / 2 ? k - n : k; };
        std::sort(active.begin(), active.end(), [&](int x, int y) { return signed_bin(x) < signed_bin(y); });
        const auto tbits = prbs15(0x1921, active.size());
        std::vector<C> training(n);
        for (size_t i = 0; i < active.size(); ++i) training[active[i]] = tbits[i] ? -1.f : 1.f;
        // Scrambler sized for 16-QAM over every symbol (covers all layouts).
        const auto scramble = prbs15(0x2A00 + user, size_t(4) * ns * nd);
        int id = user, ndata = nd, npilot = int(pilots.size());
        core = std::make_unique<FastPHY>(n, cp, ns, 1, &id, &ndata, data.data(), &npilot, pilots.data(),
                                         training.data(), scramble.data(), 4, chase_bits);
        // CP-limited channel models for the BIC fit, as the Python wrapper does.
        install_channel_models(active);
    }

    int length_samples() const { return core->length; }
    int payload_symbols(int length, int bps) const { return ((length + 4) * 8 + bps * nd - 1) / (bps * nd); }
    int frame_samples(int length, int bps) const {
        return (2 + header_symbols + payload_symbols(length, bps)) * core->length;
    }
    int header_span() const { return (2 + header_symbols) * core->length; }
    int max_frame_samples() const { return core->frame; }

    // Build a frame. Returns its sample count.
    int write(const unsigned char *info, int mcsidx, int bps, const unsigned char *payload, int length, C *out, int capacity) {
        if (bps != 2 && bps != 4) throw std::runtime_error("unsupported constellation");
        if (length < 0 || length > max_payload) throw std::runtime_error("payload exceeds configured maximum");
        if (mcsidx < 0 || mcsidx > 255) throw std::runtime_error("MCS index out of range");
        const int total = frame_samples(length, bps);
        if (capacity < total) throw std::runtime_error("short TX output buffer");
        unsigned char header[kHeaderBytes];
        header[0] = kFrameFormat;
        std::memcpy(header + 1, info, kHeaderInfoBytes);
        header[6] = (unsigned char)mcsidx; header[7] = (unsigned char)bps;
        header[8] = (unsigned char)(length >> 8); header[9] = (unsigned char)length;
        store_be32(header + 10, crc32(header, 10));
        std::vector<unsigned char> body(length + 4);
        if (length) std::memcpy(body.data(), payload, length);
        store_be32(body.data() + length, crc32(body.data(), length));
        FastPHY &f = *core;
        const User &a = f.users[0];
        const int H = header_symbols, P = payload_symbols(length, bps), L = f.length;
        const int header_bits = 2 * H * nd;
        auto bit_of = [](const unsigned char *bytes, int count_bits, int i) {
            return i < count_bits ? (bytes[i >> 3] >> (7 - (i & 7))) & 1 : 0;
        };
        for (int s = 0; s < 2 + H + P; ++s) {
            std::fill(f.fd.begin(), f.fd.end(), C{});
            if (s < 2) std::copy(f.training.begin(), f.training.end(), f.fd.begin());
            else {
                const int d = s - 2;
                for (int j = 0; j < nd; ++j) {
                    int b[4];
                    if (d < H) {
                        const int bit = 2 * (d * nd + j);
                        for (int k = 0; k < 2; ++k) b[k] = bit_of(header, kHeaderBytes * 8, bit + k) ^ a.prbs[bit + k];
                        f.fd[a.data[j]] = C(b[0] ? -Q : Q, b[1] ? -Q : Q);
                    } else {
                        const int bit = bps * ((d - H) * nd + j);
                        for (int k = 0; k < bps; ++k) b[k] = bit_of(body.data(), (length + 4) * 8, bit + k) ^ a.prbs[header_bits + bit + k];
                        f.fd[a.data[j]] = bps == 2 ? C(b[0] ? -Q : Q, b[1] ? -Q : Q)
                                                   : C((1 - 2 * b[0]) * (1 + 2 * b[1]) * A16, (1 - 2 * b[2]) * (1 + 2 * b[3]) * A16);
                    }
                }
                for (size_t j = 0; j < a.pilots.size(); ++j)
                    f.fd[a.pilots[j]] = ((d + j + a.id) % 2) ? -1.f : 1.f;
            }
            fftwf_execute(f.backward);
            C *dst = out + s * L;
            for (int j = 0; j < cp; ++j) dst[j] = f.td[n - cp + j] * f.scale;
            for (int j = 0; j < n; ++j) dst[cp + j] = f.td[j] * f.scale;
        }
        return total;
    }

    // Decode the header section of a frame acquired at `start` (needs
    // header_span() samples). metrics: [2] CFO subcarriers, [4] Chase trials.
    FrameHeader decode_header(const C *in, int start, double omega0, double *metrics) {
        FastPHY &f = *core;
        const User &a = f.users[0];
        FrameHeader fh;
        const int H = header_symbols;
        const double omega = f.prepare(in, start, omega0, header_span());
        metrics[2] = omega * n / (2 * double(PI));
        if (!f.invert(a)) return fh;
        std::vector<C> rows(H * nd);
        std::vector<double> phases(H);
        for (int s = 0; s < H; ++s) { phases[s] = f.symbol(a, s, rows.data() + s * nd); FastPHY::unwrap(phases, s); }
        double intercept, slope;
        FastPHY::phase_line(phases, H, intercept, slope);
        unsigned char header[kHeaderBytes] = {0};
        std::vector<float> confidence(kHeaderBytes * 8);
        for (int s = 0; s < H; ++s) {
            C correction = std::polar(f.scale, float(-(intercept + slope * s)));
            for (int j = 0; j < nd; ++j) {
                const int bit = 2 * (s * nd + j);
                if (bit >= kHeaderBytes * 8) break;
                int hard[4];
                float conf[4];
                FastPHY::demap(cmul(rows[s * nd + j], correction), cnorm(f.estimate[a.data[j]]), 2, hard, conf);
                for (int k = 0; k < 2 && bit + k < kHeaderBytes * 8; ++k) {
                    header[(bit + k) / 8] |= (hard[k] ^ a.prbs[bit + k]) << (7 - (bit + k) % 8);
                    confidence[bit + k] = conf[k];
                }
            }
        }
        auto valid = [&] {
            return header[0] == kFrameFormat && crc32(header, 10) == load_be32(header + 10)
                && (header[7] == 2 || header[7] == 4) && ((header[8] << 8) | header[9]) <= max_payload;
        };
        bool ok = valid();
        if (!ok && chase > 0)
            ok = FastPHY::chase_search(header, kHeaderBytes * 8, confidence.data(), chase, valid, metrics[4]);
        if (!ok) return fh;
        fh.valid = true;
        std::memcpy(fh.info, header + 1, kHeaderInfoBytes);
        fh.mcsidx = header[6]; fh.bps = header[7]; fh.length = (header[8] << 8) | header[9];
        fh.header_symbols = H;
        fh.payload_symbols = payload_symbols(fh.length, fh.bps);
        fh.samples = frame_samples(fh.length, fh.bps);
        return fh;
    }

    // Decode the payload of a frame whose header is known (needs fh.samples
    // samples from start). Returns true if the payload CRC verifies.
    // metrics: [2] CFO, [3] payload EVM dB, [4] Chase trials (accumulated).
    bool decode_payload(const C *in, int start, double omega0, const FrameHeader &fh,
                        std::vector<unsigned char> &payload, double *metrics) {
        FastPHY &f = *core;
        const User &a = f.users[0];
        const int H = fh.header_symbols, P = fh.payload_symbols, total = H + P, bps = fh.bps;
        const double omega = f.prepare(in, start, omega0, fh.samples);
        metrics[2] = omega * n / (2 * double(PI));
        payload.clear();
        if (!f.invert(a)) return false;
        std::vector<C> rows(total * nd);
        std::vector<double> phases(total);
        for (int s = 0; s < total; ++s) { phases[s] = f.symbol(a, s, rows.data() + s * nd); FastPHY::unwrap(phases, s); }
        double intercept, slope;
        FastPHY::phase_line(phases, total, intercept, slope);
        metrics[2] += slope * n / (2 * double(PI) * f.length);
        const int used = (fh.length + 4) * 8, header_bits = 2 * H * nd;
        std::vector<unsigned char> body((used + 7) / 8, 0);
        std::vector<float> confidence(used);
        double error = 0;
        int points = 0;
        for (int s = H; s < total; ++s) {
            C correction = std::polar(f.scale, float(-(intercept + slope * s)));
            for (int j = 0; j < nd; ++j) {
                const int bit = bps * ((s - H) * nd + j);
                int hard[4];
                float conf[4];
                error += FastPHY::demap(cmul(rows[s * nd + j], correction), cnorm(f.estimate[a.data[j]]), bps, hard, conf);
                ++points;
                for (int k = 0; k < bps && bit + k < used; ++k) {
                    body[(bit + k) / 8] |= (hard[k] ^ a.prbs[header_bits + bit + k]) << (7 - (bit + k) % 8);
                    confidence[bit + k] = conf[k];
                }
            }
        }
        metrics[3] = 10 * std::log10(std::max(error / std::max(points, 1), 1e-20));
        auto valid = [&] { return crc32(body.data(), fh.length) == load_be32(body.data() + fh.length); };
        bool ok = valid();
        if (!ok && chase > 0)
            ok = FastPHY::chase_search(body.data(), used, confidence.data(), chase, valid, metrics[4]);
        payload.assign(body.begin(), body.begin() + fh.length);
        return ok;
    }

private:
    static void store_be32(unsigned char *p, uint32_t v) {
        p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
    }
    void install_channel_models(const std::vector<int> &active_sorted) {
        // Least-squares projection onto CP-limited FIR responses of order
        // 1, 2, 4, 8 and cp+1 (BIC chooses per frame). Requires a small
        // pseudo-inverse per order, computed here by normal equations.
        std::vector<int> active(active_sorted);
        std::sort(active.begin(), active.end());   // FFT-index order, as in the Python wrapper
        const int count = int(active.size()), maximum = std::min(cp + 1, count);
        std::vector<int> orders;
        for (int o : {1, 2, 4, 8, maximum}) { o = std::min(o, maximum); if (std::find(orders.begin(), orders.end(), o) == orders.end()) orders.push_back(o); }
        std::sort(orders.begin(), orders.end());
        for (int order : orders) {
            std::vector<std::complex<double>> B(size_t(count) * order);
            for (int j = 0; j < count; ++j)
                for (int t = 0; t < order; ++t)
                    B[size_t(j) * order + t] = std::polar(1.0, -2 * std::acos(-1.0) * active[j] * t / n);
            // G = B^H B (order x order), solve G X = B^H for the pseudo-inverse.
            std::vector<std::complex<double>> G(size_t(order) * order), X(size_t(order) * count);
            for (int r = 0; r < order; ++r)
                for (int c = 0; c < order; ++c) {
                    std::complex<double> acc{};
                    for (int j = 0; j < count; ++j) acc += std::conj(B[size_t(j) * order + r]) * B[size_t(j) * order + c];
                    G[size_t(r) * order + c] = acc;
                }
            for (int r = 0; r < order; ++r)
                for (int j = 0; j < count; ++j) X[size_t(r) * count + j] = std::conj(B[size_t(j) * order + r]);
            // Gauss-Jordan with partial pivoting (G is Hermitian positive definite).
            for (int col = 0; col < order; ++col) {
                int piv = col;
                for (int r = col + 1; r < order; ++r) if (std::abs(G[size_t(r) * order + col]) > std::abs(G[size_t(piv) * order + col])) piv = r;
                if (piv != col) {
                    for (int c = 0; c < order; ++c) std::swap(G[size_t(col) * order + c], G[size_t(piv) * order + c]);
                    for (int j = 0; j < count; ++j) std::swap(X[size_t(col) * count + j], X[size_t(piv) * count + j]);
                }
                const std::complex<double> d = G[size_t(col) * order + col];
                for (int c = 0; c < order; ++c) G[size_t(col) * order + c] /= d;
                for (int j = 0; j < count; ++j) X[size_t(col) * count + j] /= d;
                for (int r = 0; r < order; ++r) {
                    if (r == col) continue;
                    const std::complex<double> m = G[size_t(r) * order + col];
                    if (m == 0.0) continue;
                    for (int c = 0; c < order; ++c) G[size_t(r) * order + c] -= m * G[size_t(col) * order + c];
                    for (int j = 0; j < count; ++j) X[size_t(r) * count + j] -= m * X[size_t(col) * count + j];
                }
            }
            std::vector<C> fit(size_t(order) * count), basis(size_t(count) * order);
            for (size_t i = 0; i < fit.size(); ++i) fit[i] = C(X[i]);
            for (size_t i = 0; i < basis.size(); ++i) basis[i] = C(B[i]);
            core->add_channel_model(order, count, active.data(), fit.data(), basis.data());
        }
    }
};

// Persistent framed receiver. Scans with the running Schmidl-Cox metric,
// decodes a header at each detection peak, asks `want` whether to continue
// (the production stack filters by team/next hop here), then decodes the
// payload once the announced frame length has arrived. Valid headers consume
// their whole frame; invalid ones do not, so a following preamble is found.
struct FramedStream {
    struct Result {
        long long start = 0;        // absolute sample index of frame start
        int samples = 0;            // frame length (0 if header invalid)
        bool header_valid = false, wanted = false, payload_valid = false;
        FrameHeader header;
        std::vector<unsigned char> payload;
        double sync = 0, cfo = 0, evm = 0, rssi_db = 0, chase_trials = 0;
    };
    using want_type = std::function<bool(const FrameHeader &)>;
    static constexpr double kDetect = 0.20;
    static constexpr int kCompact = 1 << 16;

    FramedPHY &phy;
    want_type want;
    std::vector<C> buffer, q;
    std::vector<float> e;
    long long base = 0;
    int pos = 0, holdoff = 0;
    bool primed = false, in_region = false, region_closed = false;
    int region_start = 0, best = 0;
    double best_metric = 0;
    CD best_p{};
    FastPHY::Autocorr ac;
    struct Pending { long long start; double omega, sync, trials; FrameHeader header; };
    std::optional<Pending> pending;

    FramedStream(FramedPHY &p, want_type w = nullptr) : phy(p), want(std::move(w)) {}
    int margin() const { return 2 * phy.core->fine_window + 8; }
    bool frame_open() const { return in_region || region_closed || pending.has_value(); }
    long long pending_samples() const { return (long long)buffer.size() - pos; }
    void reset() {
        buffer.clear(); q.clear(); e.clear();
        base = 0; pos = 0; holdoff = 0; primed = false; in_region = region_closed = false; pending.reset();
    }
    double rssi(int rel, int samples) const {
        double power = 0;
        for (int j = 0; j < samples; ++j) power += cnorm(buffer[rel + j]);
        return 10 * std::log10(std::max(power / std::max(samples, 1), 1e-30));
    }
    void consume(int next) {
        if (next > pos) { pos = next; primed = false; }
        holdoff = std::max(holdoff, next);
    }
    // Payload phase: returns false while waiting for samples.
    bool finish(bool final, std::vector<Result> &results) {
        const Pending &p = *pending;
        const int rel = int(p.start - base), size = int(buffer.size());
        if (rel + p.header.samples > size) {
            if (!final) return false;
            Result r;   // truncated by end of input
            r.start = p.start; r.header_valid = true; r.wanted = true; r.header = p.header; r.sync = p.sync;
            results.push_back(std::move(r));
            pending.reset();
            return true;
        }
        Result r;
        r.start = p.start; r.samples = p.header.samples; r.header_valid = true; r.wanted = true; r.header = p.header;
        r.sync = p.sync;
        double metrics[5] = {0, 0, 0, 0, p.trials};
        r.payload_valid = phy.decode_payload(buffer.data(), rel, p.omega, p.header, r.payload, metrics);
        r.cfo = metrics[2]; r.evm = metrics[3]; r.chase_trials = metrics[4];
        r.rssi_db = rssi(rel, p.header.samples);
        results.push_back(std::move(r));
        consume(rel + p.header.samples);
        pending.reset();
        return true;
    }
    // Header phase at the closed region's peak. Returns false if more input is needed.
    bool attempt(bool final, std::vector<Result> &results) {
        const int size = int(buffer.size()), span = phy.header_span();
        const int lo = std::max(0, best - margin()), need = best + span + margin();
        if (need > size && !final) return false;
        in_region = region_closed = false;
        holdoff = std::max(holdoff, best + phy.core->fine_window + 1);
        const int count = std::min(size, need) - lo;
        if (count < span) return true;
        int start = 0;
        double score = 0, omega = 0;
        if (!phy.core->refine(buffer.data() + lo, count, best - lo, best_p, start, score, omega, span)) return true;
        double metrics[5] = {0, 0, 0, 0, 0};
        FrameHeader fh = phy.decode_header(buffer.data() + lo, start, omega, metrics);
        Result r;
        r.start = base + lo + start; r.sync = score; r.cfo = metrics[2]; r.chase_trials = metrics[4];
        r.header_valid = fh.valid; r.header = fh;
        if (!fh.valid) { results.push_back(std::move(r)); return true; }
        r.wanted = !want || want(fh);
        if (!r.wanted) {
            r.samples = fh.samples;
            results.push_back(std::move(r));
            consume(lo + start + fh.samples);
            return true;
        }
        pending = Pending{base + lo + start, omega, score, metrics[4], fh};
        return finish(final, results);
    }
    void feed(const C *in, int count, bool final, std::vector<Result> &results) {
        for (int j = 0; j < count; ++j)
            if (!std::isfinite(in[j].real()) || !std::isfinite(in[j].imag()))
                throw std::runtime_error("stream must contain finite IQ");
        const int L = phy.length_samples();
        const int old = int(buffer.size());
        buffer.insert(buffer.end(), in, in + count);
        const int size = int(buffer.size());
        e.resize(size); FastPHY::energies(buffer.data(), old, size, e.data());
        const int qold = int(q.size()), qnew = std::max(qold, size - L);
        q.resize(qnew); FastPHY::lag_products(buffer.data(), qold, qnew, L, q.data());
        const double floor = phy.core->energy_floor();
        for (;;) {
            if (pending) { if (!finish(final, results)) break; continue; }
            if (region_closed) { if (!attempt(final, results)) break; continue; }
            if (size - pos < 2 * L + 1) break;
            if (!primed) { ac.init(q.data(), e.data(), pos, L); primed = true; }
            if (ac.above(kDetect, floor) && pos >= holdoff) {
                double m = ac.metric(floor);
                if (!in_region) { in_region = true; region_start = pos; best = pos; best_metric = m; best_p = ac.p; }
                else if (m > best_metric) { best = pos; best_metric = m; best_p = ac.p; }
            } else if (in_region && !ac.above(0.5 * kDetect, floor)) {
                region_closed = true;
            }
            if (in_region && pos - region_start >= L) region_closed = true;
            if (((pos + 1) & 1023) == 0) { ++pos; primed = false; }
            else { ac.step(q.data(), e.data(), pos, L); ++pos; }
        }
        if (final) {
            if (in_region) { region_closed = true; attempt(true, results); }
            if (pending) finish(true, results);
        }
        compact();
    }
    void compact() {
        int keep = pos;
        if (in_region || region_closed) keep = std::min(keep, best);
        if (pending) keep = std::min(keep, int(pending->start - base));
        const int keep_from = keep - margin() - 1;
        if (keep_from < kCompact) return;
        buffer.erase(buffer.begin(), buffer.begin() + keep_from);
        e.erase(e.begin(), e.begin() + keep_from);
        q.erase(q.begin(), q.begin() + std::min<int>(keep_from, int(q.size())));
        base += keep_from; pos -= keep_from; best -= keep_from; region_start -= keep_from;
        holdoff = std::max(0, holdoff - keep_from);
        primed = false;
    }
};

}  // namespace dragonradio::native

#endif  // DRAGONRADIO_SIMULATION_NATIVEFRAME_HH_
