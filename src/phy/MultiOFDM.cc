#include "Logger.hh"
#include "phy/Liquid.hh"
#include "phy/MultiOFDM.hh"

// Number of channels. We only use 1!
const unsigned int NUM_CHANNELS = 1;

// liquid fixes the header size at 8 bytes
static_assert(sizeof(Header) <= 8, "sizeof(Header) must be no more than 8 bytes");

union PHYHeader {
    Header h;
    // OFDMFLEXFRAME_H_USER in liquid.internal.h
    unsigned char bytes[8];
};

MultiOFDM::Modulator::Modulator(MultiOFDM& phy) :
    phy_(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    // modem setup (list is for parallel demodulation)
    mctx_ = std::make_unique<multichanneltx>(NUM_CHANNELS,
                                             phy_.M_,
                                             phy_.cp_len_,
                                             phy_.taper_len_,
                                             phy_.p_);
}

MultiOFDM::Modulator::~Modulator()
{
}

// Number of samples generated by a call to GenerateSamples.
const size_t NGEN = 2;

// Initial sample buffer size
const size_t MODBUF_SIZE = 16384;

void MultiOFDM::Modulator::modulate(ModPacket& mpkt, std::unique_ptr<NetPacket> pkt)
{
    PHYHeader header;

    memset(&header, 0, sizeof(header));

    header.h.curhop = pkt->curhop;
    header.h.nexthop = pkt->nexthop;
    header.h.seq = pkt->seq;
    header.h.pkt_len = pkt->size();

    pkt->resize(std::max((size_t) pkt->size(), phy_.min_pkt_size_));

    mctx_->UpdateData(0,
                      header.bytes,
                      pkt->data(),
                      pkt->size(),
                      pkt->ms,
                      pkt->fec0,
                      pkt->fec1);

    // Buffer holding generated IQ samples
    auto iqbuf = std::make_unique<IQBuf>(MODBUF_SIZE);
    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Local copy of gain
    const float g = pkt->g;

    while (!mctx_->IsChannelReadyForData(0)) {
        mctx_->GenerateSamples(&(*iqbuf)[nsamples]);

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

MultiOFDM::Demodulator::Demodulator(MultiOFDM& phy) :
    LiquidDemodulator(phy.net_),
    phy_(phy)
{
    resamp_fact_ = 2;

    std::lock_guard<std::mutex> lck(liquid_mutex);

    // modem setup (list is for parallel demodulation)
    framesync_callback callback[1] = { &LiquidDemodulator::liquid_callback };
    void               *userdata[1] = { this };

    mcrx_ = std::make_unique<multichannelrx>(NUM_CHANNELS,
                                             phy_.M_,
                                             phy_.cp_len_,
                                             phy_.taper_len_,
                                             phy_.p_,
                                             userdata,
                                             callback);
}

MultiOFDM::Demodulator::~Demodulator()
{
}

void MultiOFDM::Demodulator::reset(Clock::time_point timestamp, size_t off)
{
    mcrx_->Reset();

    demod_start_ = timestamp;
    demod_off_ = off;
}

void MultiOFDM::Demodulator::demodulate(std::complex<float>* data,
                                        size_t count,
                                        std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    mcrx_->Execute(data, count);
}

std::unique_ptr<PHY::Demodulator> MultiOFDM::make_demodulator(void)
{
    return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
}

std::unique_ptr<PHY::Modulator> MultiOFDM::make_modulator(void)
{
    return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
}
