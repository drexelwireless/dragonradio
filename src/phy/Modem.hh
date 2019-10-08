#ifndef MODEM_HH_
#define MODEM_HH_

#include <functional>

#include <Header.hh>

#include <liquid/liquid.h>

/** @brief A liquid modulation and coding scheme. */
struct MCS {
    MCS(crc_scheme check,
        fec_scheme fec0,
        fec_scheme fec1,
        modulation_scheme ms)
      : check(check)
      , fec0(fec0)
      , fec1(fec1)
      , ms(ms)
    {
    }

    MCS()
      : check(LIQUID_CRC_32)
      , fec0(LIQUID_FEC_NONE)
      , fec1(LIQUID_FEC_CONV_V27)
      , ms(LIQUID_MODEM_BPSK)
    {
    }

    bool operator ==(const MCS &other) const
    {
        return check == other.check &&
               fec0 == other.fec0 &&
               fec1 == other.fec1 &&
               ms == other.ms;
    }

    bool operator !=(const MCS &other) const
    {
        return !(*this == other);
    }

    /** @brief CRC */
    crc_scheme check;

    /** @brief FEC0 (inner FEC) */
    fec_scheme fec0;

    /** @brief FEC1 (outer FEC) */
    fec_scheme fec1;

    /** @brief Modulation scheme */
    modulation_scheme ms;

    /** @brief Get approximate rate in bps */
    float getRate(void) const
    {
        return fec_get_rate(fec0)*fec_get_rate(fec1)*modulation_types[ms].bps;
    }

    /** @brief CRC name as string */
    const char *check_name() const { return crc_scheme_str[check][0]; }

    /** @brief FEC0 (inner FEC) name as string */
    const char *fec0_name() const { return fec_scheme_str[fec0][0]; }

    /** @brief FEC1 (outer FEC) name as string */
    const char *fec1_name() const { return fec_scheme_str[fec1][0]; }

    /** @brief Modulation scheme name as string */
    const char *ms_name() const { return modulation_types[ms].name; }
};

inline void mcs2genprops(const MCS &mcs, ofdmflexframegenprops_s &props)
{
    props.check = mcs.check;
    props.fec0 = mcs.fec0;
    props.fec1 = mcs.fec1;
    props.mod_scheme = mcs.ms;
}

inline void mcs2genprops(const MCS &mcs, origflexframegenprops_s &props)
{
    props.check = mcs.check;
    props.fec0 = mcs.fec0;
    props.fec1 = mcs.fec1;
    props.mod_scheme = mcs.ms;
}

inline void mcs2genprops(const MCS &mcs, flexframegenprops_s &props)
{
    props.check = mcs.check;
    props.fec0 = mcs.fec0;
    props.fec1 = mcs.fec1;
    props.mod_scheme = mcs.ms;
}

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
