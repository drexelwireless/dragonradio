#include <liquid/liquid.h>

#include "Logger.hh"
#include "phy/FlexFrame.hh"

union PHYHeader {
    Header h;
    // FLEXFRAME_H_USER in liquid.internal.h
    unsigned char bytes[14];
};

FlexFrame::Modulator::Modulator(FlexFrame& phy) :
    phy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    flexframegenprops_init_default(&fgprops_);
    fg_ = flexframegen_create(&fgprops_);
}

FlexFrame::Modulator::~Modulator()
{
    flexframegen_destroy(fg_);
}

void FlexFrame::Modulator::print(void)
{
    flexframegen_print(fg_);
}

void FlexFrame::Modulator::update_props(NetPacket& pkt)
{
    if (fgprops_.check != pkt.check ||
        fgprops_.fec0 != pkt.fec0 ||
        fgprops_.fec1 != pkt.fec1 ||
        fgprops_.mod_scheme != pkt.ms) {
        fgprops_.check = pkt.check;
        fgprops_.fec0 = pkt.fec0;
        fgprops_.fec1 = pkt.fec1;
        fgprops_.mod_scheme = pkt.ms;

        flexframegen_setprops(fg_, &fgprops_);
    }
}

// Number of samples generated by a call to flexframegen_write_samples.
const size_t NGEN = 2;

// Initial sample buffer size
const size_t MODBUF_SIZE = 16384;

void FlexFrame::Modulator::modulate(ModPacket& mpkt, std::unique_ptr<NetPacket> pkt)
{
    PHYHeader header;

    memset(&header, 0, sizeof(header));

    header.h.src = pkt->src;
    header.h.dest = pkt->dest;
    header.h.seq = pkt->seq;
    header.h.pkt_len = pkt->size();

    pkt->resize(std::max((size_t) pkt->size(), phy_.min_pkt_size_));

    update_props(*pkt);
    flexframegen_reset(fg_);
    flexframegen_assemble(fg_, header.bytes, pkt->data(), pkt->size());

    // Buffer holding generated IQ samples
    auto iqbuf = std::make_unique<IQBuf>(MODBUF_SIZE);
    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Local copy of gain
    const float g = pkt->g;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;

    while (!last_symbol) {
        last_symbol = flexframegen_write_samples(fg_,
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

FlexFrame::Demodulator::Demodulator(FlexFrame& phy) :
    LiquidDemodulator(phy.net_),
    phy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    fs_ = flexframesync_create(&LiquidDemodulator::liquid_callback, this);
}

FlexFrame::Demodulator::~Demodulator()
{
    flexframesync_destroy(fs_);
}

void FlexFrame::Demodulator::print(void)
{
    flexframesync_print(fs_);
}

void FlexFrame::Demodulator::reset(Clock::time_point timestamp, size_t off)
{
    flexframesync_reset(fs_);

    demod_start_ = timestamp;
    demod_off_ = off;
}

void FlexFrame::Demodulator::demodulate(std::complex<float>* data,
                                        size_t count,
                                        std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    flexframesync_execute(fs_, reinterpret_cast<liquid_float_complex*>(data), count);
}

std::unique_ptr<PHY::Demodulator> FlexFrame::make_demodulator(void)
{
    return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
}

std::unique_ptr<PHY::Modulator> FlexFrame::make_modulator(void)
{
    return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
}
