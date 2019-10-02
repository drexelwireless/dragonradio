#ifndef MCS_HH_
#define MCS_HH_

#include <complex>

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

#endif /* MCS_HH_ */
