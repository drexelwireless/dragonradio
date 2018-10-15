#include <complex>

#include <liquid/liquid.h>

#include "Logger.hh"
#include "phy/LiquidPHY.hh"
#include "phy/OFDM.hh"

union PHYHeader {
    Header h;
    // OFDMFLEXFRAME_H_USER in liquid.internal.h
    unsigned char bytes[8];
};

OFDM::Modulator::Modulator(OFDM& phy)
  : LiquidModulator(phy)
  , myphy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    ofdmflexframegenprops_init_default(&fgprops_);
    fg_ = ofdmflexframegen_create(myphy_.M_,
                                  myphy_.cp_len_,
                                  myphy_.taper_len_,
                                  myphy_.p_,
                                  &fgprops_);

#if LIQUID_VERSION_NUMBER >= 1003001
    ofdmflexframegenprops_s header_props;

    mcs2flexframegenprops(phy.header_mcs_, header_props);
    ofdmflexframegen_set_header_props(fg_, &header_props);
    ofdmflexframegen_set_header_len(fg_, sizeof(Header));
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */
}

OFDM::Modulator::~Modulator()
{
    ofdmflexframegen_destroy(fg_);
}

void OFDM::Modulator::print(void)
{
    ofdmflexframegen_print(fg_);
}

void OFDM::Modulator::update_props(const TXParams &params)
{
    if (fgprops_ != params.mcs) {
        mcs2flexframegenprops(params.mcs, fgprops_);
        ofdmflexframegen_setprops(fg_, &fgprops_);
    }
}

void OFDM::Modulator::assemble(unsigned char *hdr, NetPacket& pkt)
{
    update_props(*(pkt.tx_params));
    ofdmflexframegen_reset(fg_);
    ofdmflexframegen_assemble(fg_, hdr, pkt.data(), pkt.size());
}

size_t OFDM::Modulator::maxModulatedSamples(void)
{
    return myphy_.M_ + myphy_.cp_len_;
}

bool OFDM::Modulator::modulateSamples(std::complex<float> *buf, size_t &nw)
{
    nw = myphy_.M_ + myphy_.cp_len_;

#if LIQUID_VERSION_NUMBER >= 1003000
    return ofdmflexframegen_write(fg_, buf, nw);
#else /* LIQUID_VERSION_NUMBER < 1003000 */
    return ofdmflexframegen_writesymbol(fg_, buf);
#endif /* LIQUID_VERSION_NUMBER < 1003000 */
}

OFDM::Demodulator::Demodulator(OFDM& phy)
  : LiquidDemodulator(phy)
  , myphy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    fs_ = ofdmflexframesync_create(myphy_.M_,
                                   myphy_.cp_len_,
                                   myphy_.taper_len_,
                                   myphy_.p_,
                                   &LiquidDemodulator::liquid_callback,
                                   this);

#if LIQUID_VERSION_NUMBER >= 1003001
    ofdmflexframegenprops_s header_props;

    mcs2flexframegenprops(phy.header_mcs_, header_props);
    ofdmflexframesync_set_header_props(fs_, &header_props);
    ofdmflexframesync_set_header_len(fs_, sizeof(Header));
    ofdmflexframesync_decode_header_soft(fs_, phy.soft_header_);
    ofdmflexframesync_decode_payload_soft(fs_, phy.soft_payload_);
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */
}

OFDM::Demodulator::~Demodulator()
{
    ofdmflexframesync_destroy(fs_);
}

void OFDM::Demodulator::print(void)
{
    ofdmflexframesync_print(fs_);
}

void OFDM::Demodulator::liquidReset(void)
{
    ofdmflexframesync_reset(fs_);
}

void OFDM::Demodulator::demodulateSamples(std::complex<float> *buf, const size_t n)
{
    ofdmflexframesync_execute(fs_, buf, n);
}

size_t OFDM::modulated_size(const TXParams &params, size_t n)
{
    ofdmflexframegen        fg;
    ofdmflexframegenprops_s fgprops;
    size_t                  nsymbols;

    // Copy TXParams to framegen props
    mcs2flexframegenprops(params.mcs, fgprops);

    // Create framegen object
    {
        std::lock_guard<std::mutex> lck(liquid_mutex);

        fg = ofdmflexframegen_create(M_, cp_len_, taper_len_, p_, &fgprops);
    }

    // Set framegen header props
#if LIQUID_VERSION_NUMBER >= 1003001
    ofdmflexframegenprops_s header_props;

    mcs2flexframegenprops(params.mcs, header_props);
    ofdmflexframegen_set_header_props(fg, &header_props);
    ofdmflexframegen_set_header_len(fg, sizeof(Header));
#endif /* LIQUID_VERSION_NUMBER >= 1003001 */

    // Create dummy data and assemble frame
    std::vector<unsigned char> hdr(sizeof(Header));
    std::vector<unsigned char> body(n);

    ofdmflexframegen_reset(fg);
    ofdmflexframegen_assemble(fg, hdr.data(), body.data(), body.size());

    // Get size of assembled frame
    nsymbols = (M_ + cp_len_)*ofdmflexframegen_getframelen(fg);

    // Destroy framegen object
    ofdmflexframegen_destroy(fg);

    return getTXUpsampleRate()*nsymbols;
}

std::unique_ptr<PHY::Demodulator> OFDM::make_demodulator(void)
{
    return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
}

std::unique_ptr<PHY::Modulator> OFDM::make_modulator(void)
{
    return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
}
