// Isolated adapter around the existing DragonRadio liquidDSP modem classes.
// No UHD, network device, packet controller, or MAC is linked into this library.
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "liquid/OFDM.hh"

struct LiquidBridge {
    // Header always QPSK/CRC32/uncoded; the payload constellation is selectable
    // (the receiver reads it from the decoded flexframe header).
    liquid::MCS mcs{LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, LIQUID_MODEM_QPSK};
    liquid::MCS payload_mcs;
    liquid::OFDMModulator mod;
    liquid::OFDMDemodulator demod;
    size_t pending = 0;
    LiquidBridge(unsigned n, unsigned cp, const std::string &mask, modulation_scheme payload = LIQUID_MODEM_QPSK)
      : payload_mcs{LIQUID_CRC_32, LIQUID_FEC_NONE, LIQUID_FEC_NONE, payload},
        mod(mcs, n, cp, 0, liquid::OFDMSubcarriers(mask)),
        demod(mcs, false, false, n, cp, 0, liquid::OFDMSubcarriers(mask)) {
        mod.setPayloadMCS(payload_mcs);
    }
};

static thread_local std::string bridge_error;
extern "C" {
const char *dr_liquid_error() { return bridge_error.c_str(); }
void *dr_liquid_create(unsigned n, unsigned cp, const char *mask) {
    try { return new LiquidBridge(n, cp, mask); }
    catch (const std::exception &e) { bridge_error = e.what(); return nullptr; }
}
// bits_per_symbol: 2 = QPSK (identical to dr_liquid_create), 4 = 16-QAM payload.
void *dr_liquid_create_mcs(unsigned n, unsigned cp, const char *mask, int bits_per_symbol) {
    try {
        if (bits_per_symbol != 2 && bits_per_symbol != 4) throw std::runtime_error("unsupported payload modulation");
        return new LiquidBridge(n, cp, mask, bits_per_symbol == 4 ? LIQUID_MODEM_QAM16 : LIQUID_MODEM_QPSK);
    } catch (const std::exception &e) { bridge_error = e.what(); return nullptr; }
}
void dr_liquid_destroy(void *handle) { delete static_cast<LiquidBridge *>(handle); }
// Length-prefixed queue for arbitrary streaming blocks, including blocks that
// complete several frames. The original single-frame ABI remains unchanged.
long dr_liquid_receive_many(void *handle, const std::complex<float> *in, size_t count,
                            unsigned char *out, size_t capacity, int reset) {
    try {
        auto &q = *static_cast<LiquidBridge *>(handle);
        if (reset) q.demod.reset();
        size_t copied = 0;
        q.demod.demodulate(in, count, [&](bool test, bool hv, bool pv,
                                        const Header *, void *payload, size_t len, void *) {
            if (test) return true;
            if (hv && pv) {
                if (len > 65535 || copied + 4 + len > capacity)
                    throw std::runtime_error("short streaming RX output buffer");
                for (int i = 0; i < 4; ++i) out[copied++] = (len >> (24 - i*8)) & 255;
                std::memcpy(out + copied, payload, len);
                copied += len;
            }
            return false;
        });
        return static_cast<long>(copied);
    } catch (const std::exception &e) { bridge_error = e.what(); return -1; }
}
long dr_liquid_assemble(void *handle, const unsigned char *payload, size_t len) {
    try {
        auto &q = *static_cast<LiquidBridge *>(handle);
        Header header{0, 1, Seq{0}, {0}};
        // Deterministic unused-symbol padding, independent of timing warm-ups.
        std::srand(2026);
        q.mod.assemble(&header, payload, len);
        // The pinned generator emits a final tail block beyond getframelen().
        // Match the original growable-buffer modulator, retaining that tail.
        q.pending = q.mod.assembledSize() + q.mod.maxModulatedSamples();
        return static_cast<long>(q.pending);
    } catch (const std::exception &e) { bridge_error = e.what(); return -1; }
}
long dr_liquid_write(void *handle, std::complex<float> *out, size_t capacity) {
    try {
        auto &q = *static_cast<LiquidBridge *>(handle);
        if (capacity < q.pending) throw std::runtime_error("short TX output buffer");
        size_t count = 0;
        bool done = false;
        while (!done) {
            size_t n = 0;
            if (count + q.mod.maxModulatedSamples() > capacity)
                throw std::runtime_error("TX size estimate exhausted");
            done = q.mod.modulateSamples(out + count, n);
            count += n;
        }
        return static_cast<long>(count);
    } catch (const std::exception &e) { bridge_error = e.what(); return -1; }
}
long dr_liquid_receive(void *handle, const std::complex<float> *in, size_t count,
                       unsigned char *out, size_t capacity, int reset) {
    try {
        auto &q = *static_cast<LiquidBridge *>(handle);
        if (reset) q.demod.reset();
        long copied = 0;
        q.demod.demodulate(in, count, [&](bool test, bool header_valid, bool payload_valid,
                                        const Header *, void *payload, size_t len, void *) {
            if (test) return true;
            if (header_valid && payload_valid) {
                if (len > capacity) throw std::runtime_error("short RX output buffer");
                if (copied) throw std::runtime_error("multiple packets in burst-only bridge");
                std::memcpy(out, payload, len);
                copied = static_cast<long>(len);
            }
            return false;
        });
        return copied;
    } catch (const std::exception &e) { bridge_error = e.what(); return -1; }
}
}
