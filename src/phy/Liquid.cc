#include "Logger.hh"
#include "NCO.hh"
#include "RadioConfig.hh"
#include "WorkQueue.hh"
#include "phy/Liquid.hh"

// Initial modulation buffer size
const size_t kInitialModbufSize = 16384;

// Stop-band attenuation for resamplers
const float kStopBandAttenuationDb = 60.0f;

std::mutex liquid_mutex;

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
  , min_packet_size(min_packet_size)
  , header_mcs_(header_mcs)
  , soft_header_(soft_header)
  , soft_payload_(soft_payload)
{
}

LiquidPHY::~LiquidPHY()
{
}

LiquidModulator::LiquidModulator(LiquidPHY &phy)
    : Modulator(phy)
    , liquid_phy_(phy)
    , shift_(0.0)
    , nco_(0.0)
{
    double rate = phy.getTXRateOversample()/phy.getMinTXRateOversample();

    upsamp_ = msresamp_crcf_create(rate, kStopBandAttenuationDb);
    upsamp_rate_ = msresamp_crcf_get_rate(upsamp_);
}

LiquidModulator::~LiquidModulator()
{
    msresamp_crcf_destroy(upsamp_);
}

void LiquidModulator::modulate(std::shared_ptr<NetPacket> pkt,
                               double shift,
                               ModPacket &mpkt)
{
    PHYHeader header;

    memset(&header, 0, sizeof(header));

    pkt->toHeader(header.h);

    pkt->resize(std::max((size_t) pkt->size(), liquid_phy_.min_packet_size));

    assemble(header.bytes, *pkt);

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

    if (shift != 0.0 || upsamp_rate_ != 1.0) {
        // Up-sample
        auto     iqbuf_up = std::make_shared<IQBuf>(1 + 2*upsamp_rate_*iqbuf->size());
        unsigned nw;

        msresamp_crcf_execute(upsamp_,
                              iqbuf->data(),
                              iqbuf->size(),
                              iqbuf_up->data(),
                              &nw);
        assert(nw <= iqbuf_up->size());
        iqbuf_up->resize(nw);
        iqbuf = iqbuf_up;

        // Mix up
        setFreqShift(shift);
        nco_.mix_up(iqbuf->data(), iqbuf->data(), iqbuf->size());
    }

    // Fill in the ModPacket
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkt);
}

void LiquidModulator::setFreqShift(double shift)
{
    if (shift_ != shift) {
        double rad = 2*M_PI*shift/phy_.getTXRate(); // Frequency shift in radians

        nco_.reset(rad);

        shift_ = shift;
    }
}

LiquidDemodulator::LiquidDemodulator(LiquidPHY &phy)
  : Demodulator(phy)
  , liquid_phy_(phy)
  , internal_oversample_fact_(1)
  , shift_(0.0)
  , nco_(0.0)
{
    double rate = phy.getMinRXRateOversample()/phy.getRXRateOversample();

    downsamp_ = msresamp_crcf_create(rate, kStopBandAttenuationDb);
    downsamp_rate_ = msresamp_crcf_get_rate(downsamp_);
}

LiquidDemodulator::~LiquidDemodulator()
{
    msresamp_crcf_destroy(downsamp_);
}

int LiquidDemodulator::liquid_callback(unsigned char *  header_,
                                       int              header_valid_,
                                       unsigned char *  payload_,
                                       unsigned int     payload_len_,
                                       int              payload_valid_,
                                       framesyncstats_s stats_,
                                       void *           userdata_)
{
    return reinterpret_cast<LiquidDemodulator*>(userdata_)->
        callback(header_,
                 header_valid_,
                 payload_,
                 payload_len_,
                 payload_valid_,
                 stats_);
}

int LiquidDemodulator::callback(unsigned char *  header_,
                                int              header_valid_,
                                unsigned char *  payload_,
                                unsigned int     payload_len_,
                                int              payload_valid_,
                                framesyncstats_s stats_)
{
    Header* h = reinterpret_cast<Header*>(header_);
    size_t  off = demod_off_;   // Save demodulation offset for use when we log.
    double  resamp_fact = internal_oversample_fact_/downsamp_rate_;

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    demod_off_ += resamp_fact*stats_.end_counter;

    if (header_valid_ && h->curhop == liquid_phy_.getNodeId())
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
                        payload_len_,
                        std::move(buf));
    }

    return 0;
}

void LiquidDemodulator::reset(Clock::time_point timestamp, size_t off)
{
    liquidReset();

    demod_start_ = timestamp;
    demod_off_ = off;

    msresamp_crcf_reset(downsamp_);
}

void LiquidDemodulator::demodulate(std::complex<float>* data,
                                   size_t count,
                                   double shift,
                                   std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    if (downsamp_rate_ == 1.0 && shift == 0.0) {
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
        IQBuf    iqbuf_down(1 + 2*downsamp_rate_*iqbuf_shift.size());
        unsigned nw;

        msresamp_crcf_execute(downsamp_,
                              iqbuf_shift.data(),
                              count,
                              iqbuf_down.data(),
                              &nw);
        assert(nw <= iqbuf_down.size());

        // Demodulate
        demodulateSamples(iqbuf_down.data(), nw);
    }
}

void LiquidDemodulator::setFreqShift(double shift)
{
    // We don't reset the NCO unless we have to so as to avoid phase
    // discontinuities during demodulation.
    if (shift_ != shift) {
        double rad = 2*M_PI*shift/phy_.getRXRate(); // Frequency shift in radians

        nco_.reset(rad);

        shift_ = shift;
    }
}
