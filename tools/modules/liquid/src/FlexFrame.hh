#ifndef FLEXFRAME_HH_
#define FLEXFRAME_HH_

#include "PHY.hh"

namespace py = pybind11;

class FlexFrameModulator : public Modulator {
public:
    FlexFrameModulator();

    virtual ~FlexFrameModulator();

    void setHeaderMCS(const MCS &mcs) override;

    void setPayloadMCS(const MCS &mcs) override;

protected:
    unsigned M_;
    unsigned cp_len_;
    unsigned taper_len_;
    unsigned char *p_;

    origflexframegen fg_;
    origflexframegenprops_s fgprops_;

    void assemble(const void *header, const void *payload, const size_t payload_len) override;

    size_t maxModulatedSamples(void) override;

    bool modulateSamples(std::complex<float> *buf, size_t &nw) override;
};

class FlexFrameDemodulator : public Demodulator {
public:
    FlexFrameDemodulator(bool soft_header,
                         bool soft_payload);

    virtual ~FlexFrameDemodulator();

    void setHeaderMCS(const MCS &mcs) override;

    void reset(void) override;

protected:
    origflexframesync fs_;

    void demodulateSamples(const std::complex<float> *buf, const size_t n) override;
};

#endif /* FLEXFRAME_HH_ */
