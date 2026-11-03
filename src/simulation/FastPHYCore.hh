// Reusable native (non-liquidDSP) OFDM/OFDMA frame engine core, version 2.
// Depends only on FFTW single precision and the C++17 runtime. The C ABI
// in FastPHY.cc and a future production PHY adapter share this code, so
// the reference waveform/decoder behavior has exactly one definition.
//
// Changes from v1 (frozen in tests/reference_native_v1):
//  * Acquisition: Schmidl-Cox autocorrelation detection over the two identical
//    training blocks, segmented CFO-derotated fine timing, and resolution of the
//    one-block CFO ambiguity. Acquires to about +/-1.3 subcarrier (v1: +/-0.25).
//  * Optional Gray 16-QAM on data tones (bits_per_symbol = 4). QPSK (2) is the
//    default and is sample-identical to v1 on transmit.
//  * Configurable bounded CRC-aided Chase search (chase_bits, 0 disables).
//  * Stream: persistent buffered receiver scanning each sample once.
#ifndef DRAGONRADIO_SIMULATION_FASTPHYCORE_HH_
#define DRAGONRADIO_SIMULATION_FASTPHYCORE_HH_

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdint>
#include <numeric>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <fftw3.h>

namespace dragonradio::native {

using C = std::complex<float>;
using CD = std::complex<double>;
constexpr float PI = 3.14159265358979323846f;
constexpr float Q = 0.7071067811865475f;        // QPSK axis level
constexpr float A16 = 0.31622776601683794f;     // 16-QAM unit: 1/sqrt(10)
// Plain complex arithmetic for hot loops. std::complex operator* carries an
// Annex G NaN/infinity recovery path that blocks vectorization; inputs here are
// already checked finite, so the textbook formulas are exact replacements.
inline C cmul(C a, C b) { return C(a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()); }
inline C cmulc(C a, C b) { return C(a.real() * b.real() + a.imag() * b.imag(), a.imag() * b.real() - a.real() * b.imag()); }  // a * conj(b)
inline CD cmul(CD a, CD b) { return CD(a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()); }
inline float cnorm(C a) { return a.real() * a.real() + a.imag() * a.imag(); }
// FFTW planning is not thread-safe; one lock is shared by all including units.
inline std::mutex planner_mutex;
// IEEE 802.3 CRC-32 (zlib.crc32).
inline uint32_t crc32(const unsigned char *p, int count) {
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(256);
        for (uint32_t j = 0; j < 256; ++j) {
            uint32_t c = j;
            for (int b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0);
            t[j] = c;
        }
        return t;
    }();
    uint32_t crc = 0xffffffffU;
    for (int j = 0; j < count; ++j) crc = table[(crc ^ p[j]) & 255] ^ (crc >> 8);
    return crc ^ 0xffffffffU;
}
inline uint32_t load_be32(const unsigned char *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline bool valid_packet(const unsigned char *p, int count, int user) {
    if (count < 16 || std::memcmp(p, "DOF1", 4) || p[5] != user || p[4] > 254) return false;
    int end = 12 + (int(p[10]) << 8) + p[11];
    if (end + 4 > count) return false;
    return crc32(p, end) == load_be32(p + end);
}
struct User {
    int id;
    std::vector<int> data, pilots;
    std::vector<unsigned char> prbs;
};
struct ChannelModel {
    int order;
    std::vector<C> fit, basis;
};
// Return codes of FastPHY::receive (also exported through the C ABI).
enum : int { RX_INVALID = -1, RX_NO_ACQUISITION = -2, RX_SPECTRAL_NULL = -3, RX_EXCEPTION = -4,
             RX_HEADER_REJECT = -6 };

struct FastPHY {
    int n, cp, ns, length, frame;
    int bps, chase;
    float scale, template_energy = 0;
    // Detection/timing parameters (fixed; recorded in the docs).
    static constexpr int kSegments = 8;          // fine-timing segments over the preamble
    static constexpr float kSyncThreshold = 0.20f;  // coherent normalized score
    static constexpr float kCoherentAccept = 0.50f; // skip the segmented search above this
    // Early rejection after the first data symbol: magic + destination are 40
    // known bits; a frame for another destination (or noise) mismatches about
    // half of them, a decodable frame almost none.
    static constexpr int kHeaderBitTolerance = 3;
    int fine_window;                             // +/- samples around coarse peak
    std::vector<User> users;
    std::vector<C> training, fd, td, preamble, corrected, estimate, inverse;
    int ntaps = 0;
    std::vector<int> active;
    std::vector<C> fit, basis, taps;
    std::vector<ChannelModel> models;
    std::vector<C> derotated;
    fftwf_plan forward = nullptr, backward = nullptr;
    // Fine timing by FFT: one forward transform of the derotated window, then
    // one inverse per preamble segment against precomputed conjugate spectra.
    int fine_size = 0;
    std::vector<C> fine_in, fine_spec, fine_prod, fine_out, segment_spectra, preamble_spectrum;
    fftwf_plan fine_forward = nullptr, fine_backward = nullptr;
    FastPHY(int n_, int cp_, int ns_, int nu, const int *ids, const int *nd, const int *db,
            const int *np, const int *pb, const C *tr, const unsigned char *prbs,
            int bits_per_symbol = 2, int chase_bits = 4)
      : n(n_), cp(cp_), ns(ns_), length(n + cp), frame((ns + 2) * length),
        bps(bits_per_symbol), chase(chase_bits), scale(1.f / std::sqrt(n)), fine_window(cp_ + 16),
        training(tr, tr + n), fd(n), td(n), corrected(frame), estimate(n), inverse(n) {
        std::lock_guard<std::mutex> planning(planner_mutex);
        if (n < 16 || cp < 1 || cp >= n || ns < 1 || nu < 1 || nu > 2)
            throw std::runtime_error("invalid PHY dimensions");
        if (bps != 2 && bps != 4) throw std::runtime_error("bits_per_symbol must be 2 (QPSK) or 4 (16-QAM)");
        if (chase < 0 || chase > 8) throw std::runtime_error("chase_bits must be in [0, 8]");
        forward = fftwf_plan_dft_1d(n, reinterpret_cast<fftwf_complex *>(td.data()),
                  reinterpret_cast<fftwf_complex *>(fd.data()), FFTW_FORWARD, FFTW_ESTIMATE);
        backward = fftwf_plan_dft_1d(n, reinterpret_cast<fftwf_complex *>(fd.data()),
                   reinterpret_cast<fftwf_complex *>(td.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
        if (!forward || !backward) throw std::runtime_error("FFTW plan creation failed");
        for (int u = 0; u < nu; ++u) {
            User a{ids[u], std::vector<int>(db, db + nd[u]), std::vector<int>(pb, pb + np[u]),
                   std::vector<unsigned char>(prbs, prbs + bps * ns * nd[u])};
            for (int k : a.data) if (k < 0 || k >= n) throw std::runtime_error("invalid data bin");
            for (int k : a.pilots) if (k < 0 || k >= n) throw std::runtime_error("invalid pilot bin");
            users.push_back(std::move(a));
            db += nd[u]; pb += np[u]; prbs += bps * ns * nd[u];
        }
        std::copy(training.begin(), training.end(), fd.begin());
        fftwf_execute(backward);
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (int j = n - cp; j < n; ++j) preamble.push_back(td[j] * scale);
            for (int j = 0; j < n; ++j) preamble.push_back(td[j] * scale);
        }
        for (C z : preamble) template_energy += std::norm(z);
        // Window span is (2 * fine_window + 1) + preamble - 1 samples.
        const int span = 2 * fine_window + int(preamble.size());
        fine_size = 1;
        while (fine_size < span) fine_size *= 2;
        fine_in.assign(fine_size, C{}); fine_spec.assign(fine_size, C{});
        fine_prod.assign(fine_size, C{}); fine_out.assign(fine_size, C{});
        fine_forward = fftwf_plan_dft_1d(fine_size, reinterpret_cast<fftwf_complex *>(fine_in.data()),
                       reinterpret_cast<fftwf_complex *>(fine_spec.data()), FFTW_FORWARD, FFTW_ESTIMATE);
        fine_backward = fftwf_plan_dft_1d(fine_size, reinterpret_cast<fftwf_complex *>(fine_prod.data()),
                        reinterpret_cast<fftwf_complex *>(fine_out.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
        if (!fine_forward || !fine_backward) throw std::runtime_error("FFTW plan creation failed");
        // conj(FFT(segment placed at its preamble offset)); correlation r[k] =
        // IFFT(FFT(w) * conj(FFT(p_s)))[k] / size = sum_j w[k + j] conj(p_s[j]).
        const int seg = int(preamble.size()) / kSegments;
        segment_spectra.assign(size_t(kSegments) * fine_size, C{});
        preamble_spectrum.assign(fine_size, C{});
        for (int g = 0; g < kSegments; ++g) {
            std::fill(fine_in.begin(), fine_in.end(), C{});
            for (int j = g * seg; j < (g + 1) * seg; ++j) fine_in[j] = preamble[j];
            fftwf_execute(fine_forward);
            for (int k = 0; k < fine_size; ++k) {
                segment_spectra[size_t(g) * fine_size + k] = std::conj(fine_spec[k]);
                preamble_spectrum[k] += std::conj(fine_spec[k]);   // linearity: whole preamble
            }
        }
    }
    ~FastPHY() {
        std::lock_guard<std::mutex> planning(planner_mutex);
        if (forward) fftwf_destroy_plan(forward);
        if (backward) fftwf_destroy_plan(backward);
        if (fine_forward) fftwf_destroy_plan(fine_forward);
        if (fine_backward) fftwf_destroy_plan(fine_backward);
    }
    FastPHY(const FastPHY &) = delete;
    FastPHY &operator=(const FastPHY &) = delete;

    // Validated channel-model registration.
    void add_channel_model(int order, int count, const int *bins, const C *fit_, const C *basis_) {
        if (order < 1 || order > cp + 1 || count < order || count > n)
            throw std::runtime_error("invalid channel projection dimensions");
        ntaps = std::max(order, ntaps);
        active.assign(bins, bins + count);
        models.push_back(ChannelModel{order, std::vector<C>(fit_, fit_ + order * count),
                                      std::vector<C>(basis_, basis_ + order * count)});
        taps.resize(ntaps);
    }
    // Validated burst modulation; returns the frame length in samples.
    int write_checked(const unsigned char *bytes, const int *sizes, C *out, int capacity) {
        if (capacity < frame) throw std::runtime_error("short TX output buffer");
        for (size_t u = 0; u < users.size(); ++u)
            if (sizes[u] < 0 || sizes[u] * 8 > int(users[u].prbs.size())) throw std::runtime_error("oversized packet");
        write(bytes, sizes, out);
        return frame;
    }
    int bits_capacity(const User &a) const { return bps * ns * int(a.data.size()); }

    // Map one tone's scrambled bits to a unit-average-power constellation point.
    C map(const int *b) const {
        if (bps == 2) return C(b[0] ? -Q : Q, b[1] ? -Q : Q);
        // Gray per axis: (sign, magnitude) -> (1-2s)(1+2m)/sqrt(10)
        return C((1 - 2 * b[0]) * (1 + 2 * b[1]) * A16, (1 - 2 * b[2]) * (1 + 2 * b[3]) * A16);
    }
    void write(const unsigned char *bytes, const int *sizes, C *out) {
        // Constellation size fixed at compile time keeps the per-tone loop as
        // tight as v1's QPSK-only writer.
        if (bps == 2) write_impl<2>(bytes, sizes, out);
        else write_impl<4>(bytes, sizes, out);
    }
    template <int BPS>
    void write_impl(const unsigned char *bytes, const int *sizes, C *out) {
        for (int s = 0; s < ns + 2; ++s) {
            std::fill(fd.begin(), fd.end(), C{});
            if (s < 2) std::copy(training.begin(), training.end(), fd.begin());
            else {
                int byte_offset = 0;
                for (size_t u = 0; u < users.size(); ++u) {
                    const User &a = users[u];
                    const int nd = int(a.data.size()), limit = sizes[u] * 8;
                    const unsigned char *src = bytes + byte_offset, *prbs = a.prbs.data();
                    for (int j = 0; j < nd; ++j) {
                        const int bit = BPS * ((s - 2) * nd + j);
                        int b[BPS];
                        for (int k = 0; k < BPS; ++k) {
                            const int i = bit + k;
                            b[k] = (i < limit ? ((src[i >> 3] >> (7 - (i & 7))) & 1) : 0) ^ prbs[i];
                        }
                        if constexpr (BPS == 2) fd[a.data[j]] = C(b[0] ? -Q : Q, b[1] ? -Q : Q);
                        else fd[a.data[j]] = C((1 - 2 * b[0]) * (1 + 2 * b[1]) * A16, (1 - 2 * b[2]) * (1 + 2 * b[3]) * A16);
                    }
                    for (size_t j = 0; j < a.pilots.size(); ++j)
                        fd[a.pilots[j]] = ((s - 2 + j + a.id) % 2) ? -1.f : 1.f;
                    byte_offset += sizes[u];
                }
            }
            fftwf_execute(backward);
            C *dst = out + s * length;
            for (int j = 0; j < cp; ++j) dst[j] = td[n - cp + j] * scale;
            for (int j = 0; j < n; ++j) dst[cp + j] = td[j] * scale;
        }
    }

    // Schmidl-Cox running sums over two identical blocks of L samples, built
    // from precomputed lag products q[i] = conj(y[i]) y[i+L] and energies
    // e[i] = |y[i]|^2 (each computed once per sample, vectorizable).
    struct Autocorr {
        CD p{};
        double e1 = 0, e2 = 0;
        void init(const C *q, const float *e, int d, int L) {
            p = {}; e1 = e2 = 0;
            for (int m = 0; m < L; ++m) { p += CD(q[d + m]); e1 += e[d + m]; e2 += e[d + L + m]; }
        }
        // Slide from window start d to d+1; requires q[d+L] and e[d+2L].
        void step(const C *q, const float *e, int d, int L) {
            p += CD(q[d + L]) - CD(q[d]);
            e1 += double(e[d + L]) - e[d];
            e2 += double(e[d + 2 * L]) - e[d + L];
        }
        // metric >= threshold, without a division per sample.
        bool above(double threshold, double floor) const {
            double den = std::max(e1, 0.0) * std::max(e2, 0.0);
            return den > floor && std::norm(p) >= threshold * den;
        }
        double metric(double floor) const {
            double den = std::max(e1, 0.0) * std::max(e2, 0.0);
            return den > floor ? std::norm(p) / den : 0.0;
        }
    };
    static void lag_products(const C *y, int from, int to, int L, C *q) {
        for (int i = from; i < to; ++i) q[i] = cmulc(y[i + L], y[i]);
    }
    static void energies(const C *y, int from, int to, float *e) {
        for (int i = from; i < to; ++i) e[i] = cnorm(y[i]);
    }
    std::vector<C> qbuf;
    std::vector<float> ebuf, fine_total;
    std::vector<double> fine_energy;
    double energy_floor() const {
        // Below this the window is digital silence, not a weak signal.
        return 1e-24 * double(template_energy) * double(template_energy);
    }

    // Fine timing and CFO resolution around a coarse peak. Writes the chosen
    // start, coherent score and CFO (rad/sample). Returns false if no
    // candidate reaches the coherent threshold.
    bool refine(const C *in, int count, int coarse, CD p, int &start, double &score, double &omega, int need = 0) {
        const int plen = int(preamble.size()), L = length;
        const int candidates = count - (need > 0 ? need : frame) + 1;
        const int lo = std::max(0, coarse - fine_window), hi = std::min(candidates - 1, coarse + fine_window);
        if (hi < lo) return false;
        const double omega0 = std::arg(p) / L;
        // Derotate the search window once by the coarse CFO estimate.
        const int span = hi - lo + plen;
        std::fill(fine_in.begin(), fine_in.end(), C{});
        C rotation = std::polar(1.f, float(-omega0)), phase(1, 0);
        for (int j = 0; j < span; ++j) {
            fine_in[j] = cmul(in[lo + j], phase);
            phase = cmul(phase, rotation);
            if (j % 256 == 255) phase /= std::abs(phase);
        }
        fftwf_execute(fine_forward);
        const int positions = hi - lo + 1;
        const float *spec = reinterpret_cast<const float *>(fine_spec.data());
        auto correlate = [&](const C *reference) {
            const float *ref = reinterpret_cast<const float *>(reference);
            float *prod = reinterpret_cast<float *>(fine_prod.data());
            for (int k = 0; k < fine_size; ++k) {
                const float ar = spec[2 * k], ai = spec[2 * k + 1], br = ref[2 * k], bi = ref[2 * k + 1];
                prod[2 * k] = ar * br - ai * bi;
                prod[2 * k + 1] = ar * bi + ai * br;
            }
            fftwf_execute(fine_backward);
            return reinterpret_cast<const float *>(fine_out.data());
        };
        // Sliding window energy for every candidate position.
        std::vector<double> &window = fine_energy;
        window.resize(positions);
        double energy = 0;
        for (int j = 0; j < plen; ++j) energy += cnorm(fine_in[j]);
        for (int d = 0; d < positions; ++d) {
            window[d] = energy;
            if (d + 1 < positions) energy += double(cnorm(fine_in[d + plen])) - cnorm(fine_in[d]);
        }
        // Fast path: full coherent correlation at the coarse CFO. When the
        // residual CFO is small (no block ambiguity) this peak is sharp and
        // exact; it is also the h = 0 hypothesis score.
        {
            const float *o = correlate(preamble_spectrum.data());
            const double norm = double(fine_size) * fine_size * template_energy;
            int at = lo;
            double top = -1;
            for (int d = 0; d < positions; ++d) {
                double value = window[d] > 1e-12 * template_energy
                    ? (double(o[2 * d]) * o[2 * d] + double(o[2 * d + 1]) * o[2 * d + 1]) / (norm * window[d]) : 0;
                if (value > top) { top = value; at = lo + d; }
            }
            if (top >= kCoherentAccept) {
                start = at; score = std::min(top, 1.0); omega = omega0;
                return true;
            }
        }
        // Segmented (noncoherent across segments) correlation tolerates the
        // residual CFO left by the one-block ambiguity of omega0.
        std::vector<float> &total = fine_total;
        total.assign(positions, 0.f);
        for (int g = 0; g < kSegments; ++g) {
            const float *o = correlate(segment_spectra.data() + size_t(g) * fine_size);
            for (int d = 0; d < positions; ++d) total[d] += o[2 * d] * o[2 * d] + o[2 * d + 1] * o[2 * d + 1];
        }
        int best = lo;
        double best_value = -1;
        for (int d = 0; d < positions; ++d) {
            double value = window[d] > 0 ? total[d] / window[d] : 0;
            if (value > best_value) { best_value = value; best = lo + d; }
        }
        // Resolve the CFO ambiguity of 2*pi/L with a full coherent correlation.
        energy = 0;
        for (int j = 0; j < plen; ++j) energy += cnorm(in[best + j]);
        if (energy <= 1e-12 * template_energy) return false;
        double best_score = -1, best_omega = omega0;
        for (int h = -1; h <= 1; ++h) {
            const double w = omega0 + 2 * double(PI) * h / L;
            CD acc{};
            CD r = std::polar(1.0, -w), ph = std::polar(1.0, -w * best);
            for (int j = 0; j < plen; ++j) {
                acc += cmul(CD(cmulc(in[best + j], preamble[j])), ph);
                ph = cmul(ph, r);
            }
            double sc = std::norm(acc) / (energy * template_energy);
            if (sc > best_score) { best_score = sc; best_omega = w; }
        }
        start = best; score = std::min(best_score, 1.0); omega = best_omega;
        return score >= kSyncThreshold;
    }

    // Returns packed demapper bytes; caller verifies the packet header and CRC.
    // metrics: start, sync score, CFO (subcarriers), EVM dB, Chase CRC trials.
    int receive(const C *in, int count, int user_id, unsigned char *out, int capacity, double *metrics) {
        const auto it = std::find_if(users.begin(), users.end(), [=](const User &a){ return a.id == user_id; });
        metrics[0] = 0; metrics[1] = 0; metrics[2] = 0; metrics[3] = 0; metrics[4] = 0;
        if (it == users.end() || count < frame) return RX_INVALID;
        for (int j = 0; j < count; ++j)
            if (!std::isfinite(in[j].real()) || !std::isfinite(in[j].imag())) return RX_INVALID;
        const int candidates = count - frame + 1, L = length;
        // Coarse detection: maximum Schmidl-Cox metric over every complete-frame start.
        const int span = candidates - 1 + 2 * L + 1;   // samples touched by the scan
        qbuf.resize(span); ebuf.resize(span);
        lag_products(in, 0, span - L, L, qbuf.data());
        energies(in, 0, span, ebuf.data());
        Autocorr ac;
        ac.init(qbuf.data(), ebuf.data(), 0, L);
        const double floor = energy_floor();
        double best_metric = -1;
        int coarse = 0;
        CD coarse_p{};
        for (int d = 0; d < candidates; ++d) {
            double m = ac.metric(floor);
            if (m > best_metric) { best_metric = m; coarse = d; coarse_p = ac.p; }
            if (d + 1 < candidates) {
                if (((d + 1) & 1023) == 0) ac.init(qbuf.data(), ebuf.data(), d + 1, L);  // bound drift
                else ac.step(qbuf.data(), ebuf.data(), d, L);
            }
        }
        metrics[0] = coarse;
        int start = coarse;
        double score = 0, omega = 0;
        if (best_metric <= 0 || !refine(in, count, coarse, coarse_p, start, score, omega)) {
            metrics[0] = start; metrics[1] = score;
            return RX_NO_ACQUISITION;
        }
        metrics[0] = start; metrics[1] = score;
        return decode(in, start, omega, *it, out, capacity, metrics);
    }

    // Refine the fractional CFO on the repeated training symbols (keeping the
    // integer block ambiguity chosen during acquisition), derotate `samples`
    // samples from `start` into `corrected`, and estimate the channel from the
    // training with a CP-limited FIR model chosen by BIC. Returns rad/sample.
    double prepare(const C *in, int start, double omega0, int samples) {
        const int L = length;
        if (samples > int(corrected.size())) throw std::runtime_error("frame exceeds receiver buffer");
        CD repeated{};
        for (int j = 0; j < n; ++j)
            repeated += CD(cmulc(in[start + L + cp + j], in[start + cp + j]));
        double fractional = std::arg(repeated) / L;
        double wrap = 2 * double(PI) / L;
        double omega = fractional + wrap * std::round((omega0 - fractional) / wrap);
        C rotation = std::polar(1.f, float(-omega)), phase(1, 0);
        for (int j = 0; j < samples; ++j) {
            corrected[j] = cmul(in[start + j], phase);
            phase = cmul(phase, rotation);
            if (j % 256 == 255) phase /= std::abs(phase);
        }
        std::fill(estimate.begin(), estimate.end(), C{});
        for (int s = 0; s < 2; ++s) {
            std::copy(corrected.begin() + s * L + cp, corrected.begin() + (s + 1) * L, td.begin());
            fftwf_execute(forward);
            for (int k = 0; k < n; ++k)
                if (cnorm(training[k]) > 0) estimate[k] += cmulc(fd[k], training[k]) * (0.5f * scale);
        }
        if (!models.empty()) {
            std::vector<C> best_estimate(active.size()), prediction(active.size());
            double best_score = INFINITY;
            for (const auto &model : models) {
                for (int t = 0; t < model.order; ++t) {
                    C value{};
                    for (size_t j = 0; j < active.size(); ++j)
                        value += cmul(model.fit[t * active.size() + j], estimate[active[j]]);
                    taps[t] = value;
                }
                double residual = 0;
                for (size_t j = 0; j < active.size(); ++j) {
                    C value{};
                    for (int t = 0; t < model.order; ++t) value += cmul(model.basis[j * model.order + t], taps[t]);
                    prediction[j] = value;
                    residual += cnorm(estimate[active[j]] - value);
                }
                double bic = active.size() * std::log(std::max(residual / active.size(), 1e-20))
                             + model.order * std::log(double(active.size()));
                if (bic < best_score) { best_score = bic; best_estimate = prediction; }
            }
            for (size_t j = 0; j < active.size(); ++j) estimate[active[j]] = best_estimate[j];
        }
        return omega;
    }
    // Zero-forcing inverse on the user's tones; false on a spectral null.
    bool invert(const User &a) {
        for (int k : a.data) if (std::abs(estimate[k]) < 1e-8) return false;
        for (int k : a.pilots) if (std::abs(estimate[k]) < 1e-8) return false;
        for (int k : a.data) inverse[k] = std::conj(estimate[k]) / std::norm(estimate[k]);
        for (int k : a.pilots) inverse[k] = std::conj(estimate[k]) / std::norm(estimate[k]);
        return true;
    }
    // Equalize data symbol s (0 = first after training) into row; returns the
    // raw common pilot phase of that symbol.
    double symbol(const User &a, int s, C *row) {
        const int L = length;
        std::copy(corrected.begin() + (s + 2) * L + cp, corrected.begin() + (s + 3) * L, td.begin());
        fftwf_execute(forward);
        C common{};
        for (size_t j = 0; j < a.pilots.size(); ++j) {
            int k = a.pilots[j];
            common += cmul(fd[k], inverse[k]) * (((s + j + a.id) % 2) ? -1.f : 1.f);
        }
        for (size_t j = 0; j < a.data.size(); ++j)
            row[j] = cmul(fd[a.data[j]], inverse[a.data[j]]);
        return std::arg(common);
    }
    static void unwrap(std::vector<double> &phases, int s) {
        if (!s) return;
        while (phases[s] - phases[s - 1] > PI) phases[s] -= 2 * PI;
        while (phases[s] - phases[s - 1] < -PI) phases[s] += 2 * PI;
    }
    // Least-squares line through per-symbol phases [first, first+count).
    static void phase_line(const std::vector<double> &phases, int count, double &intercept, double &slope) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int s = 0; s < count; ++s) { sx += s; sy += phases[s]; sxx += double(s) * s; sxy += s * phases[s]; }
        double denominator = count * sxx - sx * sx;
        slope = denominator ? (count * sxy - sx * sy) / denominator : 0;
        intercept = (sy - slope * sx) / count;
    }
    // Hard bits and max-log reliabilities for one equalized point.
    static float demap(C z, float gain, int bits_per_symbol, int *hard, float *confidence) {
        if (bits_per_symbol == 2) {
            hard[0] = z.real() < 0; hard[1] = z.imag() < 0;
            confidence[0] = std::abs(z.real()) * gain;
            confidence[1] = std::abs(z.imag()) * gain;
            return cnorm(z - C(hard[0] ? -Q : Q, hard[1] ? -Q : Q));
        }
        const float axis[2] = {z.real(), z.imag()};
        for (int c = 0; c < 2; ++c) {
            float v = axis[c], mag = std::abs(v);
            hard[2 * c] = v < 0;
            hard[2 * c + 1] = mag > 2 * A16;
            confidence[2 * c] = mag * gain;
            confidence[2 * c + 1] = std::abs(mag - 2 * A16) * gain;
        }
        return cnorm(z - C((1 - 2 * hard[0]) * (1 + 2 * hard[1]) * A16, (1 - 2 * hard[2]) * (1 + 2 * hard[3]) * A16));
    }
    // Bounded CRC-aided Chase search over the `limit` least reliable of the
    // first `used_bits` bits of out: singles then pairs. Returns true when
    // `valid()` accepts; trials counts the additional checks.
    template <class Valid>
    static bool chase_search(unsigned char *out, int used_bits, const float *confidence, int limit, Valid valid, double &trials) {
        std::vector<int> weakest(used_bits);
        std::iota(weakest.begin(), weakest.end(), 0);
        int candidates = std::min(limit, used_bits);
        std::partial_sort(weakest.begin(), weakest.begin() + candidates, weakest.end(),
                          [&](int x, int y){ return confidence[x] < confidence[y]; });
        auto flip = [&](int bit){ out[bit / 8] ^= 1 << (7 - bit % 8); };
        for (int i = 0; i < candidates; ++i) {
            flip(weakest[i]); ++trials;
            if (valid()) return true;
            flip(weakest[i]);
        }
        for (int i = 0; i < candidates; ++i) for (int j = i + 1; j < candidates; ++j) {
            flip(weakest[i]); flip(weakest[j]); ++trials;
            if (valid()) return true;
            flip(weakest[i]); flip(weakest[j]);
        }
        return false;
    }

    int decode(const C *in, int start, double omega0, const User &a, unsigned char *out, int capacity, double *metrics) {
        const double omega = prepare(in, start, omega0, frame);
        metrics[2] = omega * n / (2 * double(PI));
        if (!invert(a)) return RX_SPECTRAL_NULL;
        const int nd = int(a.data.size());
        int bits = bps * ns * nd, bytes = (bits + 7) / 8;
        if (bytes > capacity) throw std::runtime_error("short RX output buffer");
        std::memset(out, 0, bytes);
        double error = 0;
        std::vector<C> recovered(ns * nd);
        std::vector<double> phases(ns);
        std::vector<float> confidence(bits);
        for (int s = 0; s < ns; ++s) {
            phases[s] = symbol(a, s, recovered.data() + s * nd);
            unwrap(phases, s);
            if (s == 0 && header_mismatch(a, recovered.data(), phases[0]) > kHeaderBitTolerance)
                return RX_HEADER_REJECT;
        }
        double intercept, slope;
        phase_line(phases, ns, intercept, slope);
        metrics[2] += slope * n / (2 * double(PI) * length);
        for (int s = 0; s < ns; ++s) {
            C correction = std::polar(scale, float(-(intercept + slope * s)));
            for (int j = 0; j < nd; ++j) {
                C z = cmul(recovered[s * nd + j], correction);
                int bit = bps * (s * nd + j);
                int hard[4];
                error += demap(z, cnorm(estimate[a.data[j]]), bps, hard, confidence.data() + bit);
                for (int b = 0; b < bps; ++b) out[(bit + b) / 8] |= (hard[b] ^ a.prbs[bit + b]) << (7 - (bit + b) % 8);
            }
        }
        metrics[3] = 10 * std::log10(std::max(error / (ns * nd), 1e-20));
        metrics[4] = 0;
        if (chase > 0 && !valid_packet(out, bytes, a.id)) {
            int used_bits = bits;
            int end = 12 + (int(out[10]) << 8) + out[11] + 4;
            if (end >= 16 && end <= bytes) used_bits = std::min(bits, end * 8);
            chase_search(out, used_bits, confidence.data(), chase,
                         [&]{ return valid_packet(out, bytes, a.id); }, metrics[4]);
        }
        return bytes;
    }
    // Hard-decision mismatches against "DOF1" and this destination in the
    // first data symbol (bits 0..31 and 40..47 of the packet).
    int header_mismatch(const User &a, const C *first, double angle) const {
        const int available = bps * int(a.data.size());
        if (available < 48) return 0;   // header spans symbols: no early check
        C correction = std::polar(scale, float(-angle));
        unsigned char expect[6] = {'D', 'O', 'F', '1', 0, (unsigned char)a.id};
        int mismatches = 0;
        for (int j = 0; j * bps < 48; ++j) {
            C z = cmul(first[j], correction);
            int hard[4];
            if (bps == 2) { hard[0] = z.real() < 0; hard[1] = z.imag() < 0; }
            else {
                hard[0] = z.real() < 0; hard[1] = std::abs(z.real()) > 2 * A16;
                hard[2] = z.imag() < 0; hard[3] = std::abs(z.imag()) > 2 * A16;
            }
            for (int b = 0; b < bps; ++b) {
                int bit = j * bps + b;
                if (bit >= 48 || (bit >= 32 && bit < 40)) continue;   // skip source byte
                int value = hard[b] ^ a.prbs[bit];
                mismatches += value != ((expect[bit / 8] >> (7 - bit % 8)) & 1);
            }
        }
        return mismatches;
    }
    int user_bytes(int user_id) const {
        for (const auto &a : users) if (a.id == user_id) return (bits_capacity(a) + 7) / 8;
        return -1;
    }
};

// Persistent streaming receiver for one user. Each input sample enters the
// running Schmidl-Cox metric once; a decode is attempted only at the peak of a
// region above the detection threshold. Verified packets consume their frame;
// failed candidates do not, so a following real preamble is still found.
struct Stream {
    struct Result {
        long long start = 0;  // absolute sample index of the frame start
        int status = 0;       // byte count (>= 0) or a negative RX_* code
        double metrics[5] = {0, 0, 0, 0, 0};
        std::vector<unsigned char> bytes;
    };
    static constexpr double kDetect = 0.20;       // Schmidl-Cox metric threshold
    static constexpr int kCompact = 1 << 16;
    FastPHY &phy;
    const User *user;
    std::vector<C> buffer, q;
    std::vector<float> e;
    long long base = 0;     // absolute index of buffer[0]
    int pos = 0;            // next window start to evaluate (relative)
    bool primed = false;    // running sums valid for `pos`
    FastPHY::Autocorr ac;
    bool in_region = false, region_closed = false;
    int region_start = 0, best = 0, holdoff = 0;
    double best_metric = 0;
    CD best_p{};
    std::vector<unsigned char> scratch;

    Stream(FastPHY &p, int user_id) : phy(p), user(nullptr) {
        for (const auto &a : phy.users) if (a.id == user_id) user = &a;
        if (!user) throw std::runtime_error("user has no allocation");
        scratch.resize(phy.user_bytes(user_id));
    }
    int margin() const { return 2 * phy.fine_window + 8; }
    long long pending() const { return (long long)buffer.size() - pos; }
    void reset() {
        buffer.clear(); q.clear(); e.clear();
        base = 0; pos = 0; holdoff = 0; primed = false; in_region = region_closed = false;
    }
    // Decode at the closed region's peak. Returns false if more input is needed.
    bool attempt(bool final, std::vector<Result> &results) {
        const int size = int(buffer.size());
        int lo = std::max(0, best - margin());
        int need = best + phy.frame + margin();
        if (need > size && !final) return false;
        in_region = region_closed = false;
        int hi = std::min(size, need), count = hi - lo;
        holdoff = best + phy.fine_window + 1;
        if (count < phy.frame) return true;
        int start = 0;
        double score = 0, omega = 0;
        if (!phy.refine(buffer.data() + lo, count, best - lo, best_p, start, score, omega))
            return true;   // coherent check rejected: not a frame
        Result r;
        r.metrics[1] = score;
        r.status = phy.decode(buffer.data() + lo, start, omega, *user, scratch.data(), int(scratch.size()), r.metrics);
        r.start = base + lo + start;
        r.metrics[0] = double(r.start);
        bool ok = r.status >= 0 && valid_packet(scratch.data(), r.status, user->id);
        if (r.status >= 0) r.bytes.assign(scratch.begin(), scratch.begin() + r.status);
        results.push_back(std::move(r));
        if (ok) {
            int next = lo + start + phy.frame;
            if (next > pos) { pos = next; primed = false; }
            holdoff = std::max(holdoff, next);
        }
        return true;
    }
    void feed(const C *in, int count, bool final, std::vector<Result> &results) {
        for (int j = 0; j < count; ++j)
            if (!std::isfinite(in[j].real()) || !std::isfinite(in[j].imag()))
                throw std::runtime_error("stream must contain finite IQ");
        const int L = phy.length;
        const int old = int(buffer.size());
        buffer.insert(buffer.end(), in, in + count);
        const int size = int(buffer.size());
        e.resize(size); FastPHY::energies(buffer.data(), old, size, e.data());
        const int qold = int(q.size()), qnew = std::max(qold, size - L);
        q.resize(qnew); FastPHY::lag_products(buffer.data(), qold, qnew, L, q.data());
        const double floor = phy.energy_floor();
        for (;;) {
            if (region_closed) { if (!attempt(final, results)) break; continue; }
            if (size - pos < 2 * L + 1) break;   // need y[pos .. pos+2L] to evaluate and step
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
        if (final && in_region) { region_closed = true; attempt(true, results); }
        compact();
    }
    void compact() {
        int keep = pos;
        if (in_region || region_closed) keep = std::min(keep, best);
        int keep_from = keep - margin() - 1;
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

#endif  // DRAGONRADIO_SIMULATION_FASTPHYCORE_HH_
