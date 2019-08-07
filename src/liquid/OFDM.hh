#ifndef LIQUID_OFDM_HH_
#define LIQUID_OFDM_HH_

#include <complex>
#include <functional>
#include <optional>

#include <liquid/liquid.h>

#include "dsp/FFTW.hh"
#include "liquid/PHY.hh"

namespace Liquid {

class OFDMSubcarriers : public std::vector<char> {
public:
    explicit OFDMSubcarriers(unsigned int M);
    OFDMSubcarriers(const std::string &scs);
    OFDMSubcarriers(std::initializer_list<char> init);

    OFDMSubcarriers() = delete;

    OFDMSubcarriers &operator =(const std::string &scs);

    operator std::string() const;

    void validate(void);
};

class OFDMModulator : virtual public Modulator {
public:
    OFDMModulator(unsigned M,
                  unsigned cp_len,
                  unsigned taper_len,
                  const std::optional<OFDMSubcarriers> &p)
      : M_(M)
      , cp_len_(cp_len)
      , taper_len_(taper_len)
      , p_(M)
    {
        if (p) {
            if (p->size() != M) {
                std::stringstream buffer;

                buffer << "Subcarrier allocation must have " << M
                       << "elements but got" << p->size();

                throw std::range_error(buffer.str());
            }

            p_ = *p;
        }

        std::lock_guard<std::mutex> liquid_lock(Liquid::mutex);
        std::lock_guard<std::mutex> fftw_lock(fftw::mutex);

        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(payload_mcs_, props);
        fg_ = ofdmflexframegen_create(M_,
                                      cp_len_,
                                      taper_len_,
                                      reinterpret_cast<unsigned char*>(p_.data()),
                                      &props);

        setHeaderMCS(header_mcs_);
        reconfigureHeader();
    }

    virtual ~OFDMModulator()
    {
        if (fg_) {
            std::lock_guard<std::mutex> liquid_lock(Liquid::mutex);
            std::lock_guard<std::mutex> fftw_lock(fftw::mutex);

            ofdmflexframegen_destroy(fg_);
        }
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
    OFDMSubcarriers p_;

    /* @brief OFDM flexframe generator object */
    ofdmflexframegen fg_;

    void reconfigureHeader(void) override
    {
        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(header_mcs_, props);
        ofdmflexframegen_set_header_props(fg_, &props);
        ofdmflexframegen_set_header_len(fg_, sizeof(Header));
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
                    const std::optional<OFDMSubcarriers> &p)
        : Demodulator(soft_header, soft_payload)
        , M_(M)
        , cp_len_(cp_len)
        , taper_len_(taper_len)
        , p_(M)
    {
        if (p) {
            if (p->size() != M) {
                std::stringstream buffer;

                buffer << "Subcarrier allocation must have " << M
                       << "elements but got" << p->size();

                throw std::range_error(buffer.str());
            }

            p_ = *p;
        }

        std::lock_guard<std::mutex> liquid_lock(Liquid::mutex);
        std::lock_guard<std::mutex> fftw_lock(fftw::mutex);

        fs_ = ofdmflexframesync_create(M_,
                                       cp_len_,
                                       taper_len_,
                                       reinterpret_cast<unsigned char*>(p_.data()),
                                       &Demodulator::liquid_callback,
                                       static_cast<Demodulator*>(this));

        setHeaderMCS(header_mcs_);
        reconfigureHeader();
        reconfigureSoftDecode();
    }

    virtual ~OFDMDemodulator()
    {
        if (fs_) {
            std::lock_guard<std::mutex> liquid_lock(Liquid::mutex);
            std::lock_guard<std::mutex> fftw_lock(fftw::mutex);

            ofdmflexframesync_destroy(fs_);
        }
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
    OFDMSubcarriers p_;

    /* @brief OFDM flexframe synchronizer object */
    ofdmflexframesync fs_;

    void reconfigureHeader(void) override
    {
        ofdmflexframegenprops_s props;

        mcs2flexframegenprops(header_mcs_, props);
        ofdmflexframesync_set_header_props(fs_, &props);
        ofdmflexframesync_set_header_len(fs_, sizeof(Header));
    }

    void reconfigureSoftDecode(void) override
    {
        ofdmflexframesync_decode_header_soft(fs_, soft_header_);
        ofdmflexframesync_decode_payload_soft(fs_, soft_payload_);
    }
};

}

#endif /* LIQUID_OFDM_HH_ */
