#include "OFDM.hh"

OFDMModulator::OFDMModulator(unsigned M,
                             unsigned cp_len,
                             unsigned taper_len)
  : M_(M)
  , cp_len_(cp_len)
  , taper_len_(taper_len)
  , p_(NULL)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    ofdmflexframegenprops_init_default(&fgprops_);
    fg_ = ofdmflexframegen_create(M_,
                                  cp_len_,
                                  taper_len_,
                                  p_,
                                  &fgprops_);

    setHeaderMCS(header_mcs_);
}

OFDMModulator::~OFDMModulator()
{
    ofdmflexframegen_destroy(fg_);
}

void OFDMModulator::setHeaderMCS(const MCS &mcs)
{
    header_mcs_ = mcs;

    ofdmflexframegenprops_s header_props;

    mcs2flexframegenprops(header_mcs_, header_props);
    ofdmflexframegen_set_header_props(fg_, &header_props);
    ofdmflexframegen_set_header_len(fg_, sizeof(Header));
}

void OFDMModulator::setPayloadMCS(const MCS &mcs)
{
    payload_mcs_ = mcs;

    mcs2flexframegenprops(mcs, fgprops_);
    ofdmflexframegen_setprops(fg_, &fgprops_);
}

void OFDMModulator::assemble(const void *header, const void *payload, const size_t payload_len)
{
    ofdmflexframegen_reset(fg_);
    ofdmflexframegen_assemble(fg_,
                              static_cast<const unsigned char*>(header),
                              static_cast<const unsigned char*>(payload),
                              payload_len);
}

size_t OFDMModulator::maxModulatedSamples(void)
{
    return M_ + cp_len_;
}

bool OFDMModulator::modulateSamples(std::complex<float> *buf, size_t &nw)
{
    nw = M_ + cp_len_;

    return ofdmflexframegen_write(fg_, buf, nw);
}

OFDMDemodulator::OFDMDemodulator(bool soft_header,
                                 bool soft_payload,
                                 unsigned M,
                                 unsigned cp_len,
                                 unsigned taper_len)
  : Demodulator(soft_header, soft_payload)
  , M_(M)
  , cp_len_(cp_len)
  , taper_len_(taper_len)
  , p_(NULL)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    fs_ = ofdmflexframesync_create(M_,
                                   cp_len_,
                                   taper_len_,
                                   p_,
                                   &Demodulator::liquid_callback,
                                   this);

    setHeaderMCS(header_mcs_);
}

OFDMDemodulator::~OFDMDemodulator()
{
    ofdmflexframesync_destroy(fs_);
}

void OFDMDemodulator::setHeaderMCS(const MCS &mcs)
{
    header_mcs_ = mcs;

    ofdmflexframegenprops_s header_props;

    mcs2flexframegenprops(header_mcs_, header_props);
    ofdmflexframesync_set_header_props(fs_, &header_props);
    ofdmflexframesync_set_header_len(fs_, sizeof(Header));

    ofdmflexframesync_decode_header_soft(fs_, soft_header_);
    ofdmflexframesync_decode_payload_soft(fs_, soft_payload_);
}

void OFDMDemodulator::reset(void)
{
    ofdmflexframesync_reset(fs_);
}

void OFDMDemodulator::demodulateSamples(const std::complex<float> *buf, const size_t n)
{
    ofdmflexframesync_execute(fs_, const_cast<std::complex<float>*>(buf), n);
}
