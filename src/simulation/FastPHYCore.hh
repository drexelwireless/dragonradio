// Reusable native (non-liquidDSP) OFDM/OFDMA frame engine core.
// Depends only on FFTW single precision and the C++17 runtime. The C ABI
// in FastPHY.cc and a future production PHY adapter share this code, so
// the reference waveform/decoder behavior has exactly one definition.
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
constexpr float PI = 3.14159265358979323846f;
constexpr float Q = 0.7071067811865475f;
// FFTW planning is not thread-safe; one lock is shared by all including units.
inline std::mutex planner_mutex;
inline bool valid_packet(const unsigned char *p, int count, int user) {
    if (count < 16 || std::memcmp(p, "DOF1", 4) || p[5] != user || p[4] > 254) return false;
    int end = 12 + (int(p[10]) << 8) + p[11];
    if (end + 4 > count) return false;
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
    for (int j = 0; j < end; ++j) crc = table[(crc ^ p[j]) & 255] ^ (crc >> 8);
    crc ^= 0xffffffffU;
    uint32_t expected = (uint32_t(p[end]) << 24) | (uint32_t(p[end + 1]) << 16) | (uint32_t(p[end + 2]) << 8) | p[end + 3];
    return crc == expected;
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
struct FastPHY {
    int n, cp, ns, length, frame, corr_size = 0;
    float scale, template_energy = 0;
    std::vector<User> users;
    std::vector<C> training, fd, td, preamble, corrected, estimate, inverse;
    std::vector<C> cy, ct, spectrum;
    int ntaps = 0;
    std::vector<int> active;
    std::vector<C> fit, basis, taps;
    std::vector<ChannelModel> models;
    fftwf_plan forward = nullptr, backward = nullptr, corr_forward = nullptr, corr_backward = nullptr;
    FastPHY(int n_, int cp_, int ns_, int nu, const int *ids, const int *nd, const int *db,
            const int *np, const int *pb, const C *tr, const unsigned char *prbs)
      : n(n_), cp(cp_), ns(ns_), length(n + cp), frame((ns + 2) * length), scale(1.f / std::sqrt(n)),
        training(tr, tr + n), fd(n), td(n), corrected(frame), estimate(n), inverse(n) {
        std::lock_guard<std::mutex> planning(planner_mutex);
        if (n < 16 || cp < 1 || cp >= n || ns < 1 || nu < 1 || nu > 2)
            throw std::runtime_error("invalid PHY dimensions");
        forward = fftwf_plan_dft_1d(n, reinterpret_cast<fftwf_complex *>(td.data()),
                  reinterpret_cast<fftwf_complex *>(fd.data()), FFTW_FORWARD, FFTW_ESTIMATE);
        backward = fftwf_plan_dft_1d(n, reinterpret_cast<fftwf_complex *>(fd.data()),
                   reinterpret_cast<fftwf_complex *>(td.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
        if (!forward || !backward) throw std::runtime_error("FFTW plan creation failed");
        for (int u = 0; u < nu; ++u) {
            User a{ids[u], std::vector<int>(db, db + nd[u]), std::vector<int>(pb, pb + np[u]),
                   std::vector<unsigned char>(prbs, prbs + 2 * ns * nd[u])};
            for (int k : a.data) if (k < 0 || k >= n) throw std::runtime_error("invalid data bin");
            for (int k : a.pilots) if (k < 0 || k >= n) throw std::runtime_error("invalid pilot bin");
            users.push_back(std::move(a));
            db += nd[u]; pb += np[u]; prbs += 2 * ns * nd[u];
        }
        std::copy(training.begin(), training.end(), fd.begin());
        fftwf_execute(backward);
        for (int repeat = 0; repeat < 2; ++repeat) {
            for (int j = n - cp; j < n; ++j) preamble.push_back(td[j] * scale);
            for (int j = 0; j < n; ++j) preamble.push_back(td[j] * scale);
        }
        for (C z : preamble) template_energy += std::norm(z);
    }
    ~FastPHY() {
        std::lock_guard<std::mutex> planning(planner_mutex);
        if (forward) fftwf_destroy_plan(forward);
        if (backward) fftwf_destroy_plan(backward);
        if (corr_forward) fftwf_destroy_plan(corr_forward);
        if (corr_backward) fftwf_destroy_plan(corr_backward);
    }
    void write(const unsigned char *bytes, const int *sizes, C *out) {
        for (int s = 0; s < ns + 2; ++s) {
            std::fill(fd.begin(), fd.end(), C{});
            if (s < 2) std::copy(training.begin(), training.end(), fd.begin());
            else {
                int byte_offset = 0;
                for (size_t u = 0; u < users.size(); ++u) {
                    const User &a = users[u];
                    for (size_t j = 0; j < a.data.size(); ++j) {
                        int bit = 2 * ((s - 2) * a.data.size() + j);
                        int signs[2];
                        for (int b = 0; b < 2; ++b) {
                            int k = bit + b;
                            int value = k < sizes[u] * 8 ? ((bytes[byte_offset + k / 8] >> (7 - k % 8)) & 1) : 0;
                            signs[b] = value ^ a.prbs[k];
                        }
                        fd[a.data[j]] = C(signs[0] ? -Q : Q, signs[1] ? -Q : Q);
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
    // Validated channel-model registration (formerly inline in the C ABI).
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
    void prepare_correlation(int needed) {
        int size = 1;
        while (size < needed) size *= 2;
        if (size == corr_size) return;
        std::lock_guard<std::mutex> planning(planner_mutex);
        if (corr_forward) fftwf_destroy_plan(corr_forward);
        if (corr_backward) fftwf_destroy_plan(corr_backward);
        corr_size = size;
        cy.assign(size, C{}); ct.assign(size, C{}); spectrum.assign(size, C{});
        corr_forward = fftwf_plan_dft_1d(size, reinterpret_cast<fftwf_complex *>(cy.data()),
                       reinterpret_cast<fftwf_complex *>(ct.data()), FFTW_FORWARD, FFTW_ESTIMATE);
        corr_backward = fftwf_plan_dft_1d(size, reinterpret_cast<fftwf_complex *>(ct.data()),
                        reinterpret_cast<fftwf_complex *>(cy.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
        for (size_t j = 0; j < preamble.size(); ++j) cy[j] = std::conj(preamble[preamble.size() - 1 - j]);
        fftwf_execute(corr_forward);
        spectrum = ct;
    }
    // Returns packed demapper bytes; caller verifies the packet header and CRC.
    int receive(const C *in, int count, int user_id, unsigned char *out, int capacity, double *metrics) {
        const auto it = std::find_if(users.begin(), users.end(), [=](const User &a){ return a.id == user_id; });
        if (it == users.end() || count < frame) return -1;
        for (int j = 0; j < count; ++j)
            if (!std::isfinite(in[j].real()) || !std::isfinite(in[j].imag())) return -1;
        const User &a = *it;
        const int candidates = count - frame + 1, plen = preamble.size(), needed = candidates + plen - 1;
        prepare_correlation(needed + plen - 1);
        std::fill(cy.begin(), cy.end(), C{});
        std::copy(in, in + needed, cy.begin());
        fftwf_execute(corr_forward);
        for (int j = 0; j < corr_size; ++j) ct[j] *= spectrum[j];
        fftwf_execute(corr_backward);
        double energy = 0, best = -1;
        for (int j = 0; j < plen; ++j) energy += std::norm(std::complex<double>(in[j]));
        int start = 0;
        for (int j = 0; j < candidates; ++j) {
            double score = energy > 1e-12 * template_energy ? std::norm(cy[j + plen - 1]) / (double(corr_size) * corr_size * energy * template_energy) : 0;
            if (score > 1.01) score = 0;  // reject FFT roundoff on near-zero windows
            if (score > best) { best = score; start = j; }
            if (j + 1 < candidates) energy += std::norm(std::complex<double>(in[j + plen])) - std::norm(std::complex<double>(in[j]));
        }
        metrics[0] = start; metrics[1] = best;
        if (best < 0.20) return -2;
        std::complex<double> repeated{};
        for (int j = 0; j < n; ++j)
            repeated += std::conj(std::complex<double>(in[start + cp + j])) * std::complex<double>(in[start + length + cp + j]);
        double omega = std::arg(repeated) / length;
        metrics[2] = omega * n / (2 * double(PI));
        C rotation = std::polar(1.f, float(-omega)), phase(1, 0);
        for (int j = 0; j < frame; ++j) {
            corrected[j] = in[start + j] * phase;
            phase *= rotation;
            if (j % 256 == 255) phase /= std::abs(phase);
        }
        std::fill(estimate.begin(), estimate.end(), C{});
        for (int s = 0; s < 2; ++s) {
            std::copy(corrected.begin() + s * length + cp, corrected.begin() + (s + 1) * length, td.begin());
            fftwf_execute(forward);
            for (int k = 0; k < n; ++k)
                if (std::norm(training[k]) > 0) estimate[k] += fd[k] * (0.5f * scale) * std::conj(training[k]);
        }
        // Select a CP-limited FIR order using Bayesian information criterion.
        // Short channels need fewer noisy parameters; long CP-supported paths
        // remain candidates. Model geometry is static, observations are not.
        if (!models.empty()) {
            std::vector<C> best_estimate(active.size()), prediction(active.size());
            double best_score = INFINITY;
            for (const auto &model : models) {
                for (int t = 0; t < model.order; ++t) {
                    C value{};
                    for (size_t j = 0; j < active.size(); ++j)
                        value += model.fit[t * active.size() + j] * estimate[active[j]];
                    taps[t] = value;
                }
                double residual = 0;
                for (size_t j = 0; j < active.size(); ++j) {
                    C value{};
                    for (int t = 0; t < model.order; ++t) value += model.basis[j * model.order + t] * taps[t];
                    prediction[j] = value;
                    residual += std::norm(estimate[active[j]] - value);
                }
                double score = active.size() * std::log(std::max(residual / active.size(), 1e-20))
                               + model.order * std::log(double(active.size()));
                if (score < best_score) { best_score = score; best_estimate = prediction; }
            }
            for (size_t j = 0; j < active.size(); ++j) estimate[active[j]] = best_estimate[j];
        }
        for (int k : a.data) if (std::abs(estimate[k]) < 1e-8) return -3;
        for (int k : a.pilots) if (std::abs(estimate[k]) < 1e-8) return -3;
        for (int k : a.data) inverse[k] = std::conj(estimate[k]) / std::norm(estimate[k]);
        for (int k : a.pilots) inverse[k] = std::conj(estimate[k]) / std::norm(estimate[k]);
        int bits = 2 * ns * a.data.size(), bytes = (bits + 7) / 8;
        if (bytes > capacity) throw std::runtime_error("short RX output buffer");
        std::memset(out, 0, bytes);
        double error = 0;
        std::vector<C> recovered(ns * a.data.size());
        std::vector<double> phases(ns);
        std::vector<float> confidence(bits);
        for (int s = 0; s < ns; ++s) {
            std::copy(corrected.begin() + (s + 2) * length + cp, corrected.begin() + (s + 3) * length, td.begin());
            fftwf_execute(forward);
            C common{};
            for (size_t j = 0; j < a.pilots.size(); ++j) {
                int k = a.pilots[j];
                common += fd[k] * inverse[k] * (((s + j + a.id) % 2) ? -1.f : 1.f);
            }
            double angle = std::arg(common);
            if (s) {
                while (angle - phases[s - 1] > PI) angle -= 2 * PI;
                while (angle - phases[s - 1] < -PI) angle += 2 * PI;
            }
            phases[s] = angle;
            for (size_t j = 0; j < a.data.size(); ++j)
                recovered[s * a.data.size() + j] = fd[a.data[j]] * inverse[a.data[j]];
        }
        // Fit residual CFO/common phase over this short burst. Noise on an
        // individual symbol's pilots should not rotate an entire user packet.
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int s = 0; s < ns; ++s) { sx += s; sy += phases[s]; sxx += s * s; sxy += s * phases[s]; }
        double denominator = ns * sxx - sx * sx;
        double slope = denominator ? (ns * sxy - sx * sy) / denominator : 0;
        double intercept = (sy - slope * sx) / ns;
        metrics[2] += slope * n / (2 * double(PI) * length);
        for (int s = 0; s < ns; ++s) {
            C correction = std::polar(scale, float(-(intercept + slope * s)));
            for (size_t j = 0; j < a.data.size(); ++j) {
                C z = recovered[s * a.data.size() + j] * correction;
                int bit = 2 * (s * a.data.size() + j);
                int hard[2] = {z.real() < 0, z.imag() < 0};
                float gain = std::norm(estimate[a.data[j]]);
                confidence[bit] = std::abs(z.real()) * gain;
                confidence[bit + 1] = std::abs(z.imag()) * gain;
                error += std::norm(z - C(hard[0] ? -Q : Q, hard[1] ? -Q : Q));
                for (int b = 0; b < 2; ++b) out[(bit + b) / 8] |= (hard[b] ^ a.prbs[bit + b]) << (7 - (bit + b) % 8);
            }
        }
        metrics[3] = 10 * std::log10(std::max(error / (ns * a.data.size()), 1e-20));
        metrics[4] = 0;
        if (!valid_packet(out, bytes, user_id)) {
            // Bounded CRC-aided Chase detection: only the four least reliable
            // bits are candidates, at most ten additional CRC checks. Neither
            // payload ground truth nor simulator parameters are consulted.
            int used_bits = bits;
            int end = 12 + (int(out[10]) << 8) + out[11] + 4;
            if (end >= 16 && end <= bytes) used_bits = std::min(bits, end * 8);
            std::vector<int> weakest(used_bits);
            std::iota(weakest.begin(), weakest.end(), 0);
            int candidates = std::min(4, used_bits);
            std::partial_sort(weakest.begin(), weakest.begin() + candidates, weakest.end(),
                              [&](int x, int y){ return confidence[x] < confidence[y]; });
            auto flip = [&](int bit){ out[bit / 8] ^= 1 << (7 - bit % 8); };
            for (int i = 0; i < candidates; ++i) {
                flip(weakest[i]); ++metrics[4];
                if (valid_packet(out, bytes, user_id)) return bytes;
                flip(weakest[i]);
            }
            for (int i = 0; i < candidates; ++i) for (int j = i + 1; j < candidates; ++j) {
                flip(weakest[i]); flip(weakest[j]); ++metrics[4];
                if (valid_packet(out, bytes, user_id)) return bytes;
                flip(weakest[i]); flip(weakest[j]);
            }
        }
        return bytes;
    }
};

}  // namespace dragonradio::native

#endif  // DRAGONRADIO_SIMULATION_FASTPHYCORE_HH_
