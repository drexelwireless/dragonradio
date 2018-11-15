#include "Logger.hh"
#include "RadioConfig.hh"
#include "WorkQueue.hh"
#include "dsp/NCO.hh"
#include "phy/LiquidPHY.hh"

// Initial modulation buffer size
const size_t kInitialModbufSize = 16384;

union PHYHeader {
    Header h;
    // FLEXFRAME_H_USER in liquid.internal.h. This is the largest header of any
    // of the liquid PHY implementations.
    unsigned char bytes[14];
};

LiquidPHY::LiquidPHY(NodeId node_id,
                     const MCS &header_mcs,
                     bool soft_header,
                     bool soft_payload,
                     size_t min_packet_size)
  : PHY(node_id)
  , upsamp_resamp_params(std::bind(&LiquidPHY::reconfigureTX, this))
  , downsamp_resamp_params(std::bind(&LiquidPHY::reconfigureRX, this))
  , header_mcs_(header_mcs)
  , soft_header_(soft_header)
  , soft_payload_(soft_payload)
  , min_packet_size_(min_packet_size)
{
}

LiquidPHY::Modulator::Modulator(LiquidPHY &phy)
    : PHY::Modulator(phy)
    , liquid_phy_(phy)
    , upsamp_(phy.getTXRateOversample()/phy.getMinTXRateOversample(),
              phy.upsamp_resamp_params.m,
              phy.upsamp_resamp_params.fc,
              phy.upsamp_resamp_params.As,
              phy.upsamp_resamp_params.npfb)
    , shift_(0.0)
    , nco_(0.0)
{
}

void LiquidPHY::Modulator::modulate(std::shared_ptr<NetPacket> pkt,
                                    double shift,
                                    ModPacket &mpkt)
{
    if (pending_reconfigure_.load(std::memory_order_relaxed)) {
        pending_reconfigure_.store(false, std::memory_order_relaxed);
        reconfigure();
    }

    PHYHeader header;

    memset(&header, 0, sizeof(header));

    pkt->toHeader(header.h);

    pkt->resize(std::max((size_t) pkt->size(), liquid_phy_.min_packet_size_));

    setPayloadMCS(pkt->tx_params->mcs);
    assemble(header.bytes, pkt->data(), pkt->size());

    // Buffer holding generated IQ samples
    auto iqbuf = std::make_shared<IQBuf>(kInitialModbufSize);
    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Local copy of gain
    const float g = pkt->g;
    // Max number of samples generated by modulateSamples
    const size_t kMaxModSamples = maxModulatedSamples();
    // Number of samples written
    size_t nw;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;

    while (!last_symbol) {
        last_symbol = modulateSamples(&(*iqbuf)[nsamples], nw);

        // Apply soft gain. Note that this is where nsamples is incremented.
        for (unsigned int i = 0; i < nw; i++)
            (*iqbuf)[nsamples++] *= g;

        // If we can't add another nw samples to the current IQ buffer, resize
        // it.
        if (nsamples + kMaxModSamples > iqbuf->size())
            iqbuf->resize(2*iqbuf->size());
    }

    // Resize the final buffer to the number of samples generated.
    iqbuf->resize(nsamples);

    // Pass the modulated packet to the 0dBFS estimator if requested
    if (pkt->tx_params->nestimates_0dBFS > 0) {
        --pkt->tx_params->nestimates_0dBFS;
        work_queue.submit(&TXParams::autoSoftGain0dBFS, pkt->tx_params, g, iqbuf);
    }

    if (shift != 0.0 || upsamp_.getRate() != 1.0) {
        // Up-sample
        iqbuf->append(ceil(upsamp_.getDelay()));

        auto iqbuf_up = upsamp_.resample(*iqbuf);

        iqbuf_up->delay = floor(upsamp_.getRate()*upsamp_.getDelay());

        iqbuf = iqbuf_up;

        // Mix up
        setFreqShift(shift);
        nco_.mix_up(iqbuf->data(), iqbuf->data(), iqbuf->size());
    }

    // Fill in the ModPacket
    mpkt.fc = shift;
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkt);
}

void LiquidPHY::Modulator::setFreqShift(double shift)
{
    if (shift_ != shift) {
        double rad = 2*M_PI*shift/phy_.getTXRate(); // Frequency shift in radians

        nco_.reset(rad);

        shift_ = shift;
    }
}

void LiquidPHY::Modulator::reconfigure(void)
{
    upsamp_ = Liquid::MultiStageResampler(phy_.getTXRateOversample()/phy_.getMinTXRateOversample(),
                                          liquid_phy_.upsamp_resamp_params.m,
                                          liquid_phy_.upsamp_resamp_params.fc,
                                          liquid_phy_.upsamp_resamp_params.As,
                                          liquid_phy_.upsamp_resamp_params.npfb);

    double shift = shift_;

    setFreqShift(0.0);
    setFreqShift(shift);
}

LiquidPHY::Demodulator::Demodulator(LiquidPHY &phy)
  : Liquid::Demodulator(phy.soft_header_, phy.soft_payload_)
  , PHY::Demodulator(phy)
  , liquid_phy_(phy)
  , downsamp_(phy.getMinRXRateOversample()/phy.getRXRateOversample(),
              phy.downsamp_resamp_params.m,
              phy.downsamp_resamp_params.fc,
              phy.downsamp_resamp_params.As,
              phy.downsamp_resamp_params.npfb)
  , internal_oversample_fact_(1)
  , shift_(0.0)
  , nco_(0.0)
{
}

int LiquidPHY::Demodulator::callback(unsigned char *  header_,
                                     int              header_valid_,
                                     unsigned char *  payload_,
                                     unsigned int     payload_len_,
                                     int              payload_valid_,
                                     framesyncstats_s stats_)
{
    Header* h = reinterpret_cast<Header*>(header_);
    size_t  off = demod_off_;   // Save demodulation offset for use when we log.
    double  resamp_fact = internal_oversample_fact_/downsamp_.getRate();

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    demod_off_ += resamp_fact*stats_.end_counter;

    if (header_valid_ && h->curhop == phy_.getNodeId())
        return 0;

    // Create the packet and fill it out
    std::unique_ptr<RadioPacket> pkt;

    if (!header_valid_) {
        if (rc.verbose && !rc.debug)
            fprintf(stderr, "HEADER INVALID\n");
        logEvent("PHY: invalid header");

        pkt = std::make_unique<RadioPacket>();

        pkt->setInternalFlag(kInvalidHeader);
    } else if (!payload_valid_) {
        if (rc.verbose && !rc.debug)
            fprintf(stderr, "PAYLOAD INVALID\n");
        logEvent("PHY: invalid payload");

        pkt = std::make_unique<RadioPacket>();

        pkt->setInternalFlag(kInvalidPayload);
        pkt->fromHeader(*h);
    } else {
        pkt = std::make_unique<RadioPacket>(payload_, payload_len_);

        pkt->fromHeader(*h);
        pkt->fromExtendedHeader();
    }

    pkt->evm = stats_.evm;
    pkt->rssi = stats_.rssi;
    pkt->cfo = stats_.cfo;
    pkt->fc = shift_;

    pkt->timestamp = demod_start_ + (off + resamp_fact*stats_.start_counter) / phy_.getRXRate();

    callback_(std::move(pkt));

    if (logger && logger->getCollectSource(Logger::kRecvPackets)) {
        std::shared_ptr<buffer<std::complex<float>>> buf = nullptr;

        if (logger->getCollectSource(Logger::kRecvData)) {
            buf = std::make_shared<buffer<std::complex<float>>>(stats_.num_framesyms);
            memcpy(buf->data(), stats_.framesyms, stats_.num_framesyms*sizeof(std::complex<float>));
        }

        logger->logRecv(demod_start_,
                        off + resamp_fact*stats_.start_counter,
                        off + resamp_fact*stats_.end_counter,
                        header_valid_,
                        payload_valid_,
                        *h,
                        h->curhop,
                        h->nexthop,
                        static_cast<crc_scheme>(stats_.check),
                        static_cast<fec_scheme>(stats_.fec0),
                        static_cast<fec_scheme>(stats_.fec1),
                        static_cast<modulation_scheme>(stats_.mod_scheme),
                        stats_.evm,
                        stats_.rssi,
                        stats_.cfo,
                        shift_,
                        phy_.getRXRate(),
                        payload_len_,
                        std::move(buf));
    }

    return 0;
}

void LiquidPHY::Demodulator::reset(Clock::time_point timestamp, size_t off)
{
    if (pending_reconfigure_.load(std::memory_order_relaxed)) {
        pending_reconfigure_.store(false, std::memory_order_relaxed);
        reconfigure();
    }

    reset();

    demod_start_ = timestamp;
    demod_off_ = off;

    downsamp_.reset();
}

void LiquidPHY::Demodulator::demodulate(std::complex<float>* data,
                                        size_t count,
                                        double shift,
                                        std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    if (downsamp_.getRate() == 1.0 && shift == 0.0) {
        demodulateSamples(data, count);
    } else {
        // Mix down
        IQBuf iqbuf_shift(count);

        if (shift != 0.0) {
            setFreqShift(shift);
            nco_.mix_down(data, iqbuf_shift.data(), count);
        } else
            memcpy(iqbuf_shift.data(), data, sizeof(std::complex<float>)*count);

        // Downsample
        auto iqbuf_down = downsamp_.resample(iqbuf_shift);

        // Demodulate
        demodulateSamples(iqbuf_down->data(), iqbuf_down->size());
    }
}

void LiquidPHY::Demodulator::setFreqShift(double shift)
{
    // We don't reset the NCO unless we have to so as to avoid phase
    // discontinuities during demodulation.
    if (shift_ != shift) {
        double rad = 2*M_PI*shift/phy_.getRXRate(); // Frequency shift in radians

        nco_.reset(rad);

        shift_ = shift;
    }
}

void LiquidPHY::Demodulator::reconfigure(void)
{
    downsamp_ = Liquid::MultiStageResampler(phy_.getMinRXRateOversample()/phy_.getRXRateOversample(),
                                            liquid_phy_.upsamp_resamp_params.m,
                                            liquid_phy_.upsamp_resamp_params.fc,
                                            liquid_phy_.upsamp_resamp_params.As,
                                            liquid_phy_.upsamp_resamp_params.npfb);

    double shift = shift_;

    setFreqShift(0.0);
    setFreqShift(shift);
}

size_t LiquidPHY::getModulatedSize(const TXParams &params, size_t n)
{
    std::unique_ptr<Liquid::Modulator> mod = mkLiquidModulator();

    mod->setHeaderMCS(header_mcs_);
    mod->setPayloadMCS(params.mcs);

    PHYHeader                  header;
    std::vector<unsigned char> body(n);

    memset(&header, 0, sizeof(header));

    mod->assemble(header.bytes, body.data(), body.size());

    return getTXUpsampleRate()*mod->assembledSize();
}
