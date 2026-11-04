// Framed native downlink OFDMA (P5): one burst, one shared IQ buffer, up to
// two users on disjoint tone sets, built on the framed engine (NativeFrame.hh).
//
// Wire format "native OFDMA frame v1" (format byte 0xD4):
//   2 training symbols (as native frame v3)
//   H control symbols: QPSK on every data tone, v29 r1/2 coded, soft decoded,
//   carrying a common header of 30 bytes:
//       [0]      format byte 0xD4
//       [1]      allocation (kAllocations): which tones each user slot gets
//       [2..13]  user slot 0, [14..25] user slot 1, each:
//                [0]     present (1) or empty (0)
//                [1..5]  DragonRadio Header (curhop, nexthop, seq, flags)
//                [6]     MCS index, [7] bits per tone (1..8)
//                [8]     inner FEC, [9] outer FEC (NativeFEC.hh Code)
//                [10..11] payload length in bytes, big endian
//       [26..29] CRC-32 of bytes 0..25, big endian
//   P payload symbols: user slot u's payload (as native frame v3: uncoded
//   bytes + CRC-32, or the liquid-compatible packetizer output) on its tones
//   at its own constellation; P = the longer user's symbol count; a shorter
//   user's tones are empty after its payload.
// Pilots are common to all users. Each user slot has its own PRBS-15
// scrambler. The control section uses the full band so that both receivers
// decode it at the weaker user's SNR at least as well as their own payload.
#ifndef DRAGONRADIO_SIMULATION_NATIVEOFDMA_HH_
#define DRAGONRADIO_SIMULATION_NATIVEOFDMA_HH_

#include <array>
#include <functional>
#include <optional>

#include "NativeFrame.hh"

namespace dragonradio::native {

constexpr unsigned char kOFDMAFormat = 0xD4;
constexpr int kMaxUsers = 2;
constexpr int kUserEntryBytes = 12;
constexpr int kControlBytes = 2 + kMaxUsers * kUserEntryBytes + 4;   // 30

// Allocations: share of the data tones (in data-tone order) for slots 0 and 1.
// 0: equal halves; 1: one third / two thirds; 2: two thirds / one third;
// 3: every tone to slot 0 (single user, slot 1 empty).
constexpr int kAllocations = 4;
inline std::array<int, 2> allocation_split(int alloc, int nd) {
    switch (alloc) {
        case 0: return {nd / 2, nd - nd / 2};
        case 1: return {nd / 3, nd - nd / 3};
        case 2: return {nd - nd / 3, nd / 3};
        default: return {nd, 0};
    }
}

struct OFDMAUser {
    bool present = false;
    unsigned char info[kHeaderInfoBytes] = {0, 0, 0, 0, 0};
    int mcsidx = 0, bps = 2, length = 0;
    fec::Code fec0 = fec::kNone, fec1 = fec::kNone;
    int payload_symbols = 0;
    bool coded() const { return fec0 != fec::kNone || fec1 != fec::kNone; }
};

struct OFDMAHeader {
    bool valid = false;
    int alloc = 0;
    std::array<OFDMAUser, kMaxUsers> users;
    int header_symbols = 0, payload_symbols = 0, samples = 0;
};

// What a transmitter puts in one user slot.
struct OFDMASlotInput {
    const unsigned char *info = nullptr;   // 5-byte Header; nullptr = empty slot
    int mcsidx = 0, bps = 2;
    const unsigned char *payload = nullptr;
    int length = 0;
    fec::Code fec0 = fec::kNone, fec1 = fec::kNone;
    bool coded() const { return fec0 != fec::kNone || fec1 != fec::kNone; }
};

struct FramedOFDMA {
    FramedPHY base;   // training, FFT engine, channel models, constellations
    int nd;
    std::unique_ptr<fec::detail::Stage> control_stage;
    int control_air = kControlBytes;
    int control_symbols = 0;
    std::array<std::vector<unsigned char>, kMaxUsers> scramble;
    struct Workspace {
        std::vector<C> rows;
        std::vector<double> phases;
        std::vector<unsigned char> bytes, descrambled, soft;
        std::vector<float> confidence;
    } ws;

    FramedOFDMA(int n, int cp, const std::vector<int> &data, const std::vector<int> &pilots, int max_payload,
                int max_on_air_bytes = 0)
      : base(n, cp, data, pilots, max_payload, 0, 1, max_on_air_bytes, fec::kNone), nd(int(data.size())) {
#if defined(DRAGONRADIO_NATIVE_FEC)
        control_stage = std::make_unique<fec::detail::Stage>(fec::kV29, kControlBytes);
        control_air = int(control_stage->enc_len_);
#else
        throw std::runtime_error("native OFDMA needs native FEC (DRAGONRADIO_NATIVE_FEC) for its control header");
#endif
        control_symbols = (control_air * 8 + 2 * nd - 1) / (2 * nd);
        // Room: the whole frame within the base engine's buffer (sized for
        // the largest payload at BPSK on every tone).
        const int max_ns = base.core->ns;
        for (int u = 0; u < kMaxUsers; ++u)
            scramble[u] = prbs15(0x2A40 + u, size_t(8) * max_ns * nd);
    }

    int length_samples() const { return base.length_samples(); }
    int header_span() const { return (2 + control_symbols) * base.core->length; }
    static int on_air_bytes(int length, fec::Code f0, fec::Code f1) { return FramedPHY::on_air_bytes(length, f0, f1); }
    int user_symbols(int alloc, int slot, int length, int bps, fec::Code f0, fec::Code f1) const {
        const int tones = allocation_split(alloc, nd)[slot];
        if (tones == 0) return 0;
        return (on_air_bytes(length, f0, f1) * 8 + bps * tones - 1) / (bps * tones);
    }
    int frame_samples(int payload_symbols) const { return (2 + control_symbols + payload_symbols) * base.core->length; }
    // Tones (indices into data-tone order) of a slot.
    std::pair<int, int> tone_range(int alloc, int slot) const {
        const auto split = allocation_split(alloc, nd);
        return slot == 0 ? std::make_pair(0, split[0]) : std::make_pair(split[0], split[0] + split[1]);
    }

    // Frame length for these slots (no modulation).
    int frame_samples(int alloc, const std::array<OFDMASlotInput, kMaxUsers> &in) const {
        int P = 0;
        for (int u = 0; u < kMaxUsers; ++u)
            if (in[u].info) P = std::max(P, user_symbols(alloc, u, in[u].length, in[u].bps, in[u].fec0, in[u].fec1));
        return frame_samples(P);
    }

    int write(int alloc, const std::array<OFDMASlotInput, kMaxUsers> &in, C *out, int capacity) {
        if (alloc < 0 || alloc >= kAllocations) throw std::runtime_error("invalid OFDMA allocation");
        unsigned char control[kControlBytes] = {0};
        control[0] = kOFDMAFormat;
        control[1] = (unsigned char)alloc;
        std::array<std::vector<unsigned char>, kMaxUsers> body;
        std::array<int, kMaxUsers> P{0, 0};
        for (int u = 0; u < kMaxUsers; ++u) {
            const OFDMASlotInput &s = in[u];
            if (!s.info) continue;
            if (allocation_split(alloc, nd)[u] == 0) throw std::runtime_error("user slot has no tones in this allocation");
            if (!Constellation::valid(s.bps)) throw std::runtime_error("unsupported constellation");
            if (s.length < 0 || s.length > base.max_payload) throw std::runtime_error("payload exceeds configured maximum");
            const int air = on_air_bytes(s.length, s.fec0, s.fec1);
            if (air > base.max_on_air) throw std::runtime_error("coded payload exceeds the configured maximum on-air size");
            unsigned char *e = control + 2 + u * kUserEntryBytes;
            e[0] = 1;
            std::memcpy(e + 1, s.info, kHeaderInfoBytes);
            e[6] = (unsigned char)s.mcsidx; e[7] = (unsigned char)s.bps;
            e[8] = (unsigned char)s.fec0; e[9] = (unsigned char)s.fec1;
            e[10] = (unsigned char)(s.length >> 8); e[11] = (unsigned char)s.length;
            body[u].assign(air, 0);
            if (!s.coded()) {
                if (s.length) std::memcpy(body[u].data(), s.payload, s.length);
                const uint32_t crc = crc32(body[u].data(), s.length);
                for (int k = 0; k < 4; ++k) body[u][s.length + k] = (unsigned char)(crc >> (24 - 8 * k));
            } else {
                base.packetizers.get(s.length, s.fec0, s.fec1).encode(s.payload, body[u].data());
            }
            P[u] = user_symbols(alloc, u, s.length, s.bps, s.fec0, s.fec1);
        }
        const uint32_t crc = crc32(control, kControlBytes - 4);
        for (int k = 0; k < 4; ++k) control[kControlBytes - 4 + k] = (unsigned char)(crc >> (24 - 8 * k));
        std::vector<unsigned char> control_air_bits(control_air);
#if defined(DRAGONRADIO_NATIVE_FEC)
        control_stage->encode(control, control_air_bits.data());
#endif
        const int Pmax = std::max(P[0], P[1]), H = control_symbols, total = frame_samples(Pmax);
        if (2 + H + Pmax > base.core->ns + 2) throw std::runtime_error("OFDMA frame exceeds the engine's buffer");
        if (capacity < total) throw std::runtime_error("short TX output buffer");
        FastPHY &f = *base.core;
        const User &a = f.users[0];
        const int L = f.length;
        auto bit_of = [](const unsigned char *bytes, int count_bits, int i) {
            return i < count_bits ? (bytes[i >> 3] >> (7 - (i & 7))) & 1 : 0;
        };
        for (int s = 0; s < 2 + H + Pmax; ++s) {
            std::fill(f.fd.begin(), f.fd.end(), C{});
            if (s < 2) std::copy(f.training.begin(), f.training.end(), f.fd.begin());
            else {
                const int d = s - 2;
                if (d < H) {
                    for (int j = 0; j < nd; ++j) {
                        const int bit = 2 * (d * nd + j);
                        const int b0 = bit_of(control_air_bits.data(), control_air * 8, bit) ^ base.scramble_[bit];
                        const int b1 = bit_of(control_air_bits.data(), control_air * 8, bit + 1) ^ base.scramble_[bit + 1];
                        f.fd[a.data[j]] = C(b0 ? -Q : Q, b1 ? -Q : Q);
                    }
                } else {
                    const int p = d - H;
                    for (int u = 0; u < kMaxUsers; ++u) {
                        if (!in[u].info || p >= P[u]) continue;
                        const auto [lo, hi] = tone_range(alloc, u);
                        const int tones = hi - lo, bps = in[u].bps, air_bits = int(body[u].size()) * 8;
                        const Constellation &cons = Constellation::get(bps);
                        for (int t = 0; t < tones; ++t) {
                            const int bit = bps * (p * tones + t);
                            int b[8];
                            for (int k = 0; k < bps; ++k) b[k] = bit_of(body[u].data(), air_bits, bit + k) ^ scramble[u][bit + k];
                            f.fd[a.data[lo + t]] = cons.map(b);
                        }
                    }
                }
                for (size_t j = 0; j < a.pilots.size(); ++j)
                    f.fd[a.pilots[j]] = ((d + j + a.id) % 2) ? -1.f : 1.f;
            }
            fftwf_execute(f.backward);
            C *dst = out + s * L;
            for (int j = 0; j < base.cp; ++j) dst[j] = f.td[base.n - base.cp + j] * f.scale;
            for (int j = 0; j < base.n; ++j) dst[base.cp + j] = f.td[j] * f.scale;
        }
        return total;
    }

    OFDMAHeader decode_header(const C *in, int start, double omega0, double *metrics) {
        FastPHY &f = *base.core;
        const User &a = f.users[0];
        OFDMAHeader oh;
        const int H = control_symbols;
        const double omega = f.prepare(in, start, omega0, header_span());
        metrics[2] = omega * base.n / (2 * double(PI));
        if (!f.invert(a)) return oh;
        ws.rows.resize(size_t(H) * nd);
        ws.phases.resize(H);
        for (int s = 0; s < H; ++s) { ws.phases[s] = f.symbol(a, s, ws.rows.data() + s * nd); FastPHY::unwrap(ws.phases, s); }
        double intercept, slope;
        FastPHY::phase_line(ws.phases, H, intercept, slope);
        const int air_bits = control_air * 8;
        ws.descrambled.assign(air_bits, 0);
        ws.confidence.resize(air_bits);
        double noise = 0;
        int points = 0;
        for (int s = 0; s < H; ++s) {
            C correction = std::polar(f.scale, float(-(intercept + slope * s)));
            for (int j = 0; j < nd; ++j) {
                const int bit = 2 * (s * nd + j);
                if (bit >= air_bits) break;
                int hard[4];
                float conf[4];
                const float gain = cnorm(f.estimate[a.data[j]]);
                noise += FastPHY::demap(cmul(ws.rows[s * nd + j], correction), gain, 2, hard, conf) * gain;
                ++points;
                for (int k = 0; k < 2 && bit + k < air_bits; ++k) {
                    ws.descrambled[bit + k] = (unsigned char)(hard[k] ^ base.scramble_[bit + k]);
                    ws.confidence[bit + k] = conf[k];
                }
            }
        }
        unsigned char control[kControlBytes] = {0};
#if defined(DRAGONRADIO_NATIVE_FEC)
        ws.soft.resize(air_bits);
        FramedPHY::soft_bits(ws.descrambled.data(), ws.confidence.data(), air_bits, noise / std::max(points, 1), Q, ws.soft.data());
        control_stage->decode_soft(ws.soft.data(), control);
#endif
        if (control[0] != kOFDMAFormat || crc32(control, kControlBytes - 4) != load_be32(control + kControlBytes - 4))
            return oh;
        oh.alloc = control[1];
        if (oh.alloc >= kAllocations) return oh;
        int Pmax = 0;
        for (int u = 0; u < kMaxUsers; ++u) {
            const unsigned char *e = control + 2 + u * kUserEntryBytes;
            OFDMAUser &usr = oh.users[u];
            usr.present = e[0] == 1;
            if (!usr.present) continue;
            std::memcpy(usr.info, e + 1, kHeaderInfoBytes);
            usr.mcsidx = e[6]; usr.bps = e[7];
            usr.fec0 = fec::Code(e[8]); usr.fec1 = fec::Code(e[9]);
            usr.length = (e[10] << 8) | e[11];
            if (!Constellation::valid(usr.bps) || !fec::valid(e[8]) || !fec::valid(e[9]) || usr.length > base.max_payload
                || on_air_bytes(usr.length, usr.fec0, usr.fec1) > base.max_on_air || allocation_split(oh.alloc, nd)[u] == 0)
                return oh;
            usr.payload_symbols = user_symbols(oh.alloc, u, usr.length, usr.bps, usr.fec0, usr.fec1);
            Pmax = std::max(Pmax, usr.payload_symbols);
        }
        if (2 + H + Pmax > base.core->ns + 2) return oh;
        oh.valid = true;
        oh.header_symbols = H;
        oh.payload_symbols = Pmax;
        oh.samples = frame_samples(Pmax);
        return oh;
    }

    // Decode one user slot's payload. Returns true if its CRC verifies.
    bool decode_user(const C *in, int start, double omega0, const OFDMAHeader &oh, int slot,
                     std::vector<unsigned char> &payload, double *metrics) {
        FastPHY &f = *base.core;
        const User &a = f.users[0];
        const OFDMAUser &usr = oh.users[slot];
        const int H = oh.header_symbols, total = H + oh.payload_symbols, P = usr.payload_symbols, bps = usr.bps;
        const auto [lo, hi] = tone_range(oh.alloc, slot);
        const int tones = hi - lo;
        const Constellation &cons = Constellation::get(bps);
        const double omega = f.prepare(in, start, omega0, oh.samples);
        metrics[2] = omega * base.n / (2 * double(PI));
        payload.clear();
        if (!f.invert(a)) return false;
        ws.rows.resize(size_t(total) * nd);
        ws.phases.resize(total);
        for (int s = 0; s < total; ++s) { ws.phases[s] = f.symbol(a, s, ws.rows.data() + s * nd); FastPHY::unwrap(ws.phases, s); }
        double intercept, slope;
        FastPHY::phase_line(ws.phases, total, intercept, slope);
        const int used = on_air_bytes(usr.length, usr.fec0, usr.fec1) * 8;
        const bool soft_input = usr.coded() && base.soft && fec::detail::conv(usr.fec1);
        ws.bytes.assign((used + 7) / 8, 0);
        ws.confidence.resize(used);
        ws.descrambled.resize(soft_input ? used : 0);
        double error = 0, noise = 0;
        int points = 0;
        for (int p = 0; p < P; ++p) {
            const int s = H + p;
            C correction = std::polar(f.scale, float(-(intercept + slope * s)));
            for (int t = 0; t < tones; ++t) {
                const int j = lo + t, bit = bps * (p * tones + t);
                if (bit >= used) break;
                int hard[8];
                float conf[8];
                const float gain = cnorm(f.estimate[a.data[j]]);
                const C z = cmul(ws.rows[s * nd + j], correction);
                const float e = (bps == 2 || bps == 4) ? FastPHY::demap(z, gain, bps, hard, conf) : cons.demap(z, gain, hard, conf);
                error += e;
                noise += e * gain;
                ++points;
                for (int k = 0; k < bps && bit + k < used; ++k) {
                    const int b = hard[k] ^ scramble[slot][bit + k];
                    ws.bytes[(bit + k) / 8] |= b << (7 - (bit + k) % 8);
                    ws.confidence[bit + k] = conf[k];
                    if (soft_input) ws.descrambled[bit + k] = (unsigned char)b;
                }
            }
        }
        metrics[3] = 10 * std::log10(std::max(error / std::max(points, 1), 1e-20));
        if (usr.coded()) {
            if (!fec::kAvailable) return false;
            fec::Packetizer &pk = base.packetizers.get(usr.length, usr.fec0, usr.fec1);
            payload.assign(usr.length, 0);
            if (!soft_input)
                return pk.decode(ws.bytes.data(), payload.data());
            ws.soft.resize(used);
            FramedPHY::soft_bits(ws.descrambled.data(), ws.confidence.data(), used, noise / std::max(points, 1), cons.unit, ws.soft.data());
            return pk.decode_soft(ws.soft.data(), payload.data());
        }
        const bool ok = crc32(ws.bytes.data(), usr.length) == load_be32(ws.bytes.data() + usr.length);
        payload.assign(ws.bytes.begin(), ws.bytes.begin() + usr.length);
        return ok;
    }
};

// Persistent OFDMA receiver: as FramedStream, but a valid control header
// yields one result per present user slot. `want` decides per slot (the
// production stack filters by next hop: a UE decodes only its own slot);
// unwanted slots are not demodulated. The frame is consumed whole.
struct FramedOFDMAStream {
    struct Result {
        long long start = 0;
        int samples = 0;
        int slot = -1;                 // -1: invalid control header
        bool header_valid = false, wanted = false, payload_valid = false;
        int alloc = 0;
        OFDMAUser user;
        std::vector<unsigned char> payload;
        double cfo = 0, evm = 0, rssi_db = 0;
    };
    using want_type = std::function<bool(const OFDMAUser &)>;
    static constexpr double kDetect = 0.20;
    static constexpr int kCompact = 1 << 16;

    FramedOFDMA &phy;
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
    struct Pending { long long start; double omega; OFDMAHeader header; std::array<bool, kMaxUsers> wanted; };
    std::optional<Pending> pending;

    FramedOFDMAStream(FramedOFDMA &p, want_type w = nullptr) : phy(p), want(std::move(w)) {}
    int margin() const { return 2 * phy.base.core->fine_window + 8; }
    bool frame_open() const { return in_region || region_closed || pending.has_value(); }
    bool holds_samples() const { return frame_open() || pos != (long long)buffer.size(); }
    void reset() {
        buffer.clear(); q.clear(); e.clear();
        base = 0; pos = 0; holdoff = 0; primed = false; in_region = region_closed = false; pending.reset();
    }
    void consume(int next) {
        if (next > pos) { pos = next; primed = false; }
        holdoff = std::max(holdoff, next);
    }
    double rssi(int rel, int samples) const {
        double power = 0;
        for (int j = 0; j < samples && rel + j < int(buffer.size()); ++j) power += cnorm(buffer[rel + j]);
        return 10 * std::log10(std::max(power / std::max(samples, 1), 1e-30));
    }
    bool finish(bool final, std::vector<Result> &results) {
        const Pending &p = *pending;
        const int rel = int(p.start - base), size = int(buffer.size());
        if (rel + p.header.samples > size && !final) return false;
        const bool complete = rel + p.header.samples <= size;
        for (int u = 0; u < kMaxUsers; ++u) {
            if (!p.header.users[u].present) continue;
            Result r;
            r.start = p.start; r.samples = p.header.samples; r.slot = u; r.header_valid = true;
            r.wanted = p.wanted[u]; r.alloc = p.header.alloc; r.user = p.header.users[u];
            if (r.wanted && complete) {
                double metrics[5] = {0, 0, 0, 0, 0};
                r.payload_valid = phy.decode_user(buffer.data(), rel, p.omega, p.header, u, r.payload, metrics);
                r.cfo = metrics[2]; r.evm = metrics[3];
                r.rssi_db = rssi(rel, p.header.samples);
            }
            results.push_back(std::move(r));
        }
        consume(rel + p.header.samples);
        pending.reset();
        return true;
    }
    bool attempt(bool final, std::vector<Result> &results) {
        const int size = int(buffer.size()), span = phy.header_span();
        const int lo = std::max(0, best - margin()), need = best + span + margin();
        if (need > size && !final) return false;
        in_region = region_closed = false;
        holdoff = std::max(holdoff, best + phy.base.core->fine_window + 1);
        const int count = std::min(size, need) - lo;
        if (count < span) return true;
        int start = 0;
        double score = 0, omega = 0;
        if (!phy.base.core->refine(buffer.data() + lo, count, best - lo, best_p, start, score, omega, span)) return true;
        double metrics[5] = {0, 0, 0, 0, 0};
        OFDMAHeader oh = phy.decode_header(buffer.data() + lo, start, omega, metrics);
        if (!oh.valid) {
            Result r;
            r.start = base + lo + start;
            results.push_back(std::move(r));
            return true;
        }
        Pending p{base + lo + start, omega, oh, {false, false}};
        bool any = false;
        for (int u = 0; u < kMaxUsers; ++u)
            if (oh.users[u].present) any |= (p.wanted[u] = !want || want(oh.users[u]));
        if (!any) {   // skip the frame by length without demodulating payloads
            for (int u = 0; u < kMaxUsers; ++u) {
                if (!oh.users[u].present) continue;
                Result r;
                r.start = p.start; r.samples = oh.samples; r.slot = u; r.header_valid = true; r.alloc = oh.alloc; r.user = oh.users[u];
                results.push_back(std::move(r));
            }
            consume(lo + start + oh.samples);
            return true;
        }
        pending = p;
        return finish(final, results);
    }
    void feed(const C *in, int count, bool final, std::vector<Result> &results) {
        if (!all_finite(in, count))
            throw std::runtime_error("stream must contain finite IQ");
        const int L = phy.length_samples();
        const int old = int(buffer.size());
        buffer.insert(buffer.end(), in, in + count);
        const int size = int(buffer.size());
        e.resize(size); FastPHY::energies(buffer.data(), old, size, e.data());
        const int qold = int(q.size()), qnew = std::max(qold, size - L);
        q.resize(qnew); FastPHY::lag_products(buffer.data(), qold, qnew, L, q.data());
        const double floor = phy.base.core->energy_floor();
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
        int keep = std::min(pos, int(buffer.size()));
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

#endif  // DRAGONRADIO_SIMULATION_NATIVEOFDMA_HH_
