#ifndef LIQUID_OFDM_HH_
#define LIQUID_OFDM_HH_

#include <complex>
#include <functional>

#include <liquid/liquid.h>

#include "liquid/PHY.hh"

namespace Liquid {

class OFDMModulator : virtual public Modulator {
public:
    OFDMModulator(unsigned M,
                  unsigned cp_len,
                  unsigned taper_len,
                  const std::vector<unsigned char> &p = {})
      : M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(p)
    {
        std::lock_guard<std::mutex> lck(Liquid::mutex);

        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(payload_mcs_, props);
        fg_ = ofdmflexframegen_create(M_,
                                      cp_len_,
                                      taper_len_,
                                      p_.size() == 0 ? nullptr : const_cast<unsigned char*>(p_.data()),
                                      &props);

        setHeaderMCS(header_mcs_);
    }

    virtual ~OFDMModulator()
    {
        if (fg_)
            ofdmflexframegen_destroy(fg_);
    }

    OFDMModulator(const OFDMModulator &) = delete;
    OFDMModulator(OFDMModulator &&) = delete;

    OFDMModulator &operator=(const OFDMModulator &) = delete;
    OFDMModulator &operator=(OFDMModulator &&) = delete;

    void print(void) override
    {
        ofdmflexframegen_print(fg_);
    }

    void assemble(const void *header,
                  const void *payload,
                  const size_t payload_len) override
    {
        ofdmflexframegen_reset(fg_);
        ofdmflexframegen_assemble(fg_,
                                  static_cast<unsigned char*>(const_cast<void*>(header)),
                                  static_cast<unsigned char*>(const_cast<void*>(payload)),
                                  payload_len);
    }

    size_t assembledSize(void) override
    {
        return (M_ + cp_len_)*ofdmflexframegen_getframelen(fg_);
    }

    size_t maxModulatedSamples(void) override
    {
        return M_ + cp_len_;
    }

    bool modulateSamples(std::complex<float> *buf, size_t &nw) override
    {
        nw = M_ + cp_len_;

        return ofdmflexframegen_write(fg_, buf, nw);
    }

protected:
    /* @brief The number of subcarriers */
    unsigned M_;

    /* @brief cp_len The cyclic prefix length */
    unsigned cp_len_;

    /* @brief taper_len The taper length (OFDM symbol overlap) */
    unsigned taper_len_;

    /* @param p The subcarrier allocation (null, pilot, data). Should have M
     * entries
     */
    std::vector<unsigned char> p_;

    /* @brief OFDM flexframe generator object */
    ofdmflexframegen fg_;

    void reconfigureHeader(void) override
    {
#if LIQUID_VERSION_NUMBER >= 1003001
        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(header_mcs_, props);
        ofdmflexframegen_set_header_props(fg_, &props);
        ofdmflexframegen_set_header_len(fg_, sizeof(Header));
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */
    }

    void reconfigurePayload(void) override
    {
        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(payload_mcs_, props);
        ofdmflexframegen_setprops(fg_, &props);
    }
};

class OFDMDemodulator : virtual public Demodulator {
public:
    OFDMDemodulator(bool soft_header,
                    bool soft_payload,
                    unsigned M,
                    unsigned cp_len,
                    unsigned taper_len,
                    const std::vector<unsigned char> &p = {})
        : Demodulator(soft_header, soft_payload)
        , M_(M)
        , cp_len_(cp_len)
        , taper_len_(taper_len)
        , p_(p)
    {
        std::lock_guard<std::mutex> lck(Liquid::mutex);

        fs_ = ofdmflexframesync_create(M_,
                                       cp_len_,
                                       taper_len_,
                                       p_.size() == 0 ? nullptr : const_cast<unsigned char*>(p_.data()),
                                       &Demodulator::liquid_callback,
                                       static_cast<Demodulator*>(this));

        setHeaderMCS(header_mcs_);
    }

    virtual ~OFDMDemodulator()
    {
        if (fs_)
            ofdmflexframesync_destroy(fs_);
    }

    OFDMDemodulator(const OFDMDemodulator &) = delete;
    OFDMDemodulator(OFDMDemodulator &&) = delete;

    OFDMDemodulator &operator==(const OFDMDemodulator &) = delete;
    OFDMDemodulator &operator!=(const OFDMDemodulator &) = delete;

    bool isFrameOpen(void) override
    {
        return ofdmflexframesync_is_frame_open(fs_);
    }

    void print(void) override
    {
        ofdmflexframesync_print(fs_);
    }

    void reset(void) override
    {
        ofdmflexframesync_reset(fs_);
    }

    void demodulateSamples(const std::complex<float> *buf,
                           const size_t n) override
    {
        ofdmflexframesync_execute(fs_, const_cast<std::complex<float>*>(buf), n);
    }

protected:
    /* @brief The number of subcarriers */
    unsigned M_;

    /* @brief cp_len The cyclic prefix length */
    unsigned cp_len_;

    /* @brief taper_len The taper length (OFDM symbol overlap) */
    unsigned taper_len_;

    /* @param p The subcarrier allocation (null, pilot, data). Should have M
     * entries
     */
    std::vector<unsigned char> p_;

    /* @brief OFDM flexframe synchronizer object */
    ofdmflexframesync fs_;

    void reconfigureHeader(void) override
    {
#if LIQUID_VERSION_NUMBER >= 1003001
        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(header_mcs_, props);
        ofdmflexframesync_set_header_props(fs_, &props);
        ofdmflexframesync_set_header_len(fs_, sizeof(Header));
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */
    }

    void reconfigureSoftDecode(void) override
    {
#if LIQUID_VERSION_NUMBER >= 1003001
        ofdmflexframesync_decode_header_soft(fs_, soft_header_);
        ofdmflexframesync_decode_payload_soft(fs_, soft_payload_);
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */
    }
};

}

#endif /* LIQUID_OFDM_HH_ */
