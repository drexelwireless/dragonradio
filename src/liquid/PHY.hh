#ifndef LIQUID_PHY_HH_
#define LIQUID_PHY_HH_

#include <complex>
#include <functional>

#include <liquid/liquid.h>

#include "Header.hh"
#include "liquid/Mutex.hh"
#include "phy/Modem.hh"

namespace Liquid {

class Modulator {
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

    virtual unsigned getOversampleRate(void)
    {
        return 1;
    }

    virtual void print(void) = 0;

    virtual void assemble(const void *header,
                          const void *payload,
                          const size_t payload_len) = 0;

    virtual size_t assembledSize(void) = 0;

    virtual size_t maxModulatedSamples(void) = 0;

    virtual bool modulateSamples(std::complex<float> *out, size_t &nw) = 0;

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

class Demodulator {
public:
    /** @brief The type of demodulation callbacks */
    using callback_t = std::function<int(const Header*,
                                         bool,
                                         bool,
                                         void*,
                                         size_t,
                                         bool,
                                         framesyncstats_s)>;

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

    virtual unsigned getOversampleRate(void)
    {
        return 1;
    }

    /** @brief Is a frame currently being demodulated?
     * @return true if a frame is currently being demodulated, false
     * otherwise.
     */
    virtual bool isFrameOpen(void) = 0;

    virtual void print(void) = 0;

    virtual void reset(void) = 0;

    virtual void demodulate(const std::complex<float> *in,
                            const size_t n,
                            callback_t cb);

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

#endif /* LIQUID_PHY_HH_ */
