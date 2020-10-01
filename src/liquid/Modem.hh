// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef LIQUID_MODEM_HH_
#define LIQUID_MODEM_HH_

#include <complex>
#include <functional>

#include <liquid/liquid.h>

#include "Header.hh"
#include "liquid/Mutex.hh"
#include "phy/Modem.hh"

namespace liquid {

/** @brief A liquid modulation and coding scheme. */
struct MCS : public ::MCS {
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

    float getRate(void) const override
    {
        return fec_get_rate(fec0)*fec_get_rate(fec1)*modulation_types[ms].bps;
    }

    std::string description(void) const override
    {
        constexpr size_t BUFSIZE = 200;
        char             buf[BUFSIZE];

        snprintf(buf, BUFSIZE, "(%s, %s, %s, %s)",
            crc_scheme_str[check][0],
            fec_scheme_str[fec0][0],
            fec_scheme_str[fec1][0],
            modulation_types[ms].name);

        return std::string(buf);
    }
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

class Modulator : public ::Modulator {
public:
    Modulator(const MCS &header_mcs)
      : header_mcs_(header_mcs)
    {
    }

    Modulator() = delete;

    virtual ~Modulator() = default;

    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    void setHeaderMCS(const MCS &mcs)
    {
        if (mcs != header_mcs_) {
            header_mcs_ = mcs;
            reconfigureHeader();
        }
    }

    const MCS &getPayloadMCS() const
    {
        return payload_mcs_;
    }

    void setPayloadMCS(const MCS &mcs)
    {
        if (mcs != payload_mcs_) {
            payload_mcs_ = mcs;
            reconfigurePayload();
        }
    }

protected:
    /** @brief Header MCS */
    MCS header_mcs_;

    /** @brief Payload MCS */
    MCS payload_mcs_;

    /** @brief Reconfigure modulator based on new header parameters */
    virtual void reconfigureHeader(void) = 0;

    /** @brief Reconfigure modulator based on new payload parameters */
    virtual void reconfigurePayload(void) = 0;
};

class Demodulator : public ::Demodulator {
public:
    Demodulator(const MCS &header_mcs,
                bool soft_header,
                bool soft_payload)
      : header_mcs_(header_mcs)
      , soft_header_(soft_header)
      , soft_payload_(soft_payload)
    {
    }

    Demodulator() = delete;

    virtual ~Demodulator() = default;

    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    void setHeaderMCS(const MCS &mcs)
    {
        if (mcs != header_mcs_) {
            header_mcs_ = mcs;
            reconfigureHeader();
        }
    }

    bool getSoftHeader() const
    {
        return soft_header_;
    }

    void setSoftHeader(bool soft)
    {
        if (soft != soft_header_) {
            soft_header_ = soft;
            reconfigureSoftDecode();
        }
    }

    bool getSoftPayload() const
    {
        return soft_payload_;
    }

    void setSoftPayload(bool soft)
    {
        if (soft != soft_payload_) {
            soft_payload_ = soft;
            reconfigureSoftDecode();
        }
    }

    void demodulate(const std::complex<float> *in,
                    const size_t n,
                    callback_t cb) override;

protected:
    /** @brief Header MCS */
    MCS header_mcs_;

    /** @brief Flag indicating whether or not to use soft decoding for header */
    bool soft_header_;

    /** @brief Flag indicating whether or not to use soft decoding for payload */
    bool soft_payload_;

    /** @brief Demodulation callback */
    callback_t cb_;

    /** @brief Demodulate samples */
    virtual void demodulateSamples(const std::complex<float> *in,
                                   const size_t n) = 0;

    /** @brief Callback function for liquid demodulator */
    static int liquid_callback(unsigned char *  header_,
                               int              header_valid_,
                               int              header_test_,
                               unsigned char *  payload_,
                               unsigned int     payload_len_,
                               int              payload_valid_,
                               framesyncstats_s stats_,
                               void *           userdata_);

    /** @brief Callback function for liquid demodulator */
    virtual int callback(unsigned char *  header_,
                         int              header_valid_,
                         int              header_test_,
                         unsigned char *  payload_,
                         unsigned int     payload_len_,
                         int              payload_valid_,
                         framesyncstats_s stats_);

    /** @brief Reconfigure demodulator based on new header parameters */
    virtual void reconfigureHeader(void) = 0;

    /** @brief Reconfigure demodulator based on new soft decoding parameters */
    virtual void reconfigureSoftDecode(void) = 0;
};

}

#endif /* LIQUID_MODEM_HH_ */
