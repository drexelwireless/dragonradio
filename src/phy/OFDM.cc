#include <liquid/liquid.h>

#include "Logger.hh"
#include "phy/Liquid.hh"
#include "phy/OFDM.hh"

union PHYHeader {
    Header h;
    // OFDMFLEXFRAME_H_USER in liquid.internal.h
    unsigned char bytes[8];
};

OFDM::Modulator::Modulator(OFDM& phy) :
    phy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    ofdmflexframegenprops_init_default(&fgprops_);
    fg_ = ofdmflexframegen_create(phy_.M_,
                                  phy_.cp_len_,
                                  phy_.taper_len_,
                                  phy_.p_,
                                  &fgprops_);
}

OFDM::Modulator::~Modulator()
{
    ofdmflexframegen_destroy(fg_);
}

void OFDM::Modulator::print(void)
{
    ofdmflexframegen_print(fg_);
}

void OFDM::Modulator::update_props(NetPacket& pkt)
{
    if (fgprops_.check != pkt.check ||
        fgprops_.fec0 != pkt.fec0 ||
        fgprops_.fec1 != pkt.fec1 ||
        fgprops_.mod_scheme != pkt.ms) {
        fgprops_.check = pkt.check;
        fgprops_.fec0 = pkt.fec0;
        fgprops_.fec1 = pkt.fec1;
        fgprops_.mod_scheme = pkt.ms;

        ofdmflexframegen_setprops(fg_, &fgprops_);
    }
}

// Initial sample buffer size
const size_t MODBUF_SIZE = 16384;

void OFDM::Modulator::modulate(ModPacket& mpkt, std::shared_ptr<NetPacket> pkt)
{
    PHYHeader header;

    memset(&header, 0, sizeof(header));

    pkt->toHeader(header.h);

    pkt->resize(std::max((size_t) pkt->size(), phy_.min_pkt_size_));

    update_props(*pkt);
    ofdmflexframegen_reset(fg_);
    ofdmflexframegen_assemble(fg_, header.bytes, pkt->data(), pkt->size());

    // Buffer holding generated IQ samples
    auto iqbuf = std::make_unique<IQBuf>(MODBUF_SIZE);
    // Number of symbols generated
    const size_t NGEN = phy_.M_ + phy_.cp_len_;
    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Local copy of gain
    const float g = pkt->g;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;

    while (!last_symbol) {
        last_symbol = ofdmflexframegen_writesymbol(fg_,
          reinterpret_cast<liquid_float_complex*>(&(*iqbuf)[nsamples]));

        // Apply soft gain. Note that this is where nsamples is incremented.
        for (unsigned int i = 0; i < NGEN; i++)
            (*iqbuf)[nsamples++] *= g;

        // If we can't add another NGEN samples to the current IQ buffer, resize
        // it.
        if (nsamples + NGEN > iqbuf->size())
            iqbuf->resize(2*iqbuf->size());
    }

    // Resize the final buffer to the number of samples generated.
    iqbuf->resize(nsamples);

    // Fill in the ModPacket
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkt);
}

OFDM::Demodulator::Demodulator(OFDM& phy) :
    phy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    fs_ = ofdmflexframesync_create(phy_.M_,
                                   phy_.cp_len_,
                                   phy_.taper_len_,
                                   phy_.p_,
                                   &LiquidDemodulator::liquid_callback,
                                   this);
}

OFDM::Demodulator::~Demodulator()
{
    ofdmflexframesync_destroy(fs_);
}

void OFDM::Demodulator::print(void)
{
    ofdmflexframesync_print(fs_);
}

void OFDM::Demodulator::reset(Clock::time_point timestamp, size_t off)
{
    ofdmflexframesync_reset(fs_);

    demod_start_ = timestamp;
    demod_off_ = off;
}

void OFDM::Demodulator::demodulate(std::complex<float>* data,
                                   size_t count,
                                   std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    ofdmflexframesync_execute(fs_, reinterpret_cast<liquid_float_complex*>(data), count);
}

std::unique_ptr<PHY::Demodulator> OFDM::make_demodulator(void)
{
    return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
}

std::unique_ptr<PHY::Modulator> OFDM::make_modulator(void)
{
    return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
}
