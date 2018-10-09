#ifndef OFDM_HH_
#define OFDM_HH_

#include "PHY.hh"

namespace py = pybind11;

class OFDMModulator : public Modulator {
public:
    OFDMModulator(unsigned M,
                  unsigned cp_len,
                  unsigned taper_len);

    virtual ~OFDMModulator();

    void setHeaderMCS(const MCS &mcs) override;

    void setPayloadMCS(const MCS &mcs) override;

protected:
    unsigned M_;
    unsigned cp_len_;
    unsigned taper_len_;
    unsigned char *p_;

    ofdmflexframegen fg_;
    ofdmflexframegenprops_s fgprops_;

    void assemble(const void *header, const void *payload, const size_t payload_len) override;

    size_t maxModulatedSamples(void) override;

    bool modulateSamples(std::complex<float> *buf, size_t &nw) override;
};

class OFDMDemodulator : public Demodulator {
public:
    OFDMDemodulator(bool soft_header,
                    bool soft_payload,
                    unsigned M,
                    unsigned cp_len,
                    unsigned taper_len);

    virtual ~OFDMDemodulator();

    void setHeaderMCS(const MCS &mcs) override;

    void reset(void) override;

protected:
    unsigned M_;
    unsigned cp_len_;
    unsigned taper_len_;
    unsigned char *p_;

    ofdmflexframesync fs_;

    void demodulateSamples(const std::complex<float> *buf, const size_t n) override;
};

#endif /* OFDM_HH_ */
