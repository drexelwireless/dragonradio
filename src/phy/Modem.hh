#ifndef MODEM_HH_
#define MODEM_HH_

#include <functional>

#include <Header.hh>

#include <liquid/liquid.h>

/** @brief A modulation and coding scheme. */
struct MCS {
    MCS() = default;
    virtual ~MCS() = default;

    /** @brief Get approximate rate in bps */
    virtual float getRate(void) const = 0;

    /** @brief Get string representation */
    virtual std::string description(void) const = 0;
};

/** @brief An index into the PHY's MCS table */
typedef unsigned mcsidx_t;

class Modulator {
public:
    Modulator() = default;
    virtual ~Modulator() = default;

    virtual unsigned getOversampleRate(void)
    {
        return 1;
    }

    /** @brief Print description of modulator to stdout */
    virtual void print(void) = 0;

    /** @brief Assemble data for modulation
     * @param header Pointer to header
     * @param payload Pointer to payload
     * @param payload_len Number of bytes in payload
     */
    virtual void assemble(const Header *header,
                          const void *payload,
                          const size_t payload_len) = 0;

    /** @brief Return size of assembled data */
    virtual size_t assembledSize(void) = 0;

    /** @brief Return maximum number of modulated samples that will be produced */
    virtual size_t maxModulatedSamples(void) = 0;

    /** @brief Modulate assembled packet
     * @param out Pointer to output buffer for IQ data
     * @param n Reference to variable that will hold number of samples produced
     */
    virtual bool modulateSamples(std::complex<float> *out, size_t &n) = 0;
};

class Demodulator {
public:
    /** @brief The type of demodulation callbacks */
    using callback_t = std::function<bool(bool,          // Is this a header test?
                                          bool,          // Header valid?
                                          bool,          // Packet valid?
                                          const Header*, // Header
                                          void*,         // Packet
                                          size_t,        // Packet sizes
                                          void*          // Extra data
                                         )>;

    Demodulator() = default;

    virtual ~Demodulator() = default;

    virtual unsigned getOversampleRate(void)
    {
        return 1;
    }

    /** @brief Is a frame currently being demodulated?
     * @return true if a frame is currently being demodulated, false
     * otherwise.
     */
    virtual bool isFrameOpen(void) = 0;

    /** @brief Print description of demodulator to stdout */
    virtual void print(void) = 0;

    /** @brief Reset demodulator state */
    virtual void reset(void) = 0;

    /** @brief Demodulate IQ data.
     * @param in Pointer to IQ data to demodulate
     * @param n Number of IQ samples to demodulate
     * @param cb Demodulation callback
     */
    virtual void demodulate(const std::complex<float> *in,
                            const size_t n,
                            callback_t cb) = 0;
};

#endif /* MODEM_HH_ */
