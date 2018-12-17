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

LiquidPHY::LiquidPHY(std::shared_ptr<SnapshotCollector> collector,
                     NodeId node_id,
                     const MCS &header_mcs,
                     bool soft_header,
                     bool soft_payload,
                     size_t min_packet_size)
  : PHY(node_id)
  , snapshot_collector_(collector)
  , header_mcs_(header_mcs)
  , soft_header_(soft_header)
  , soft_payload_(soft_payload)
  , min_packet_size_(min_packet_size)
{
}

LiquidPHY::Modulator::Modulator(LiquidPHY &phy)
    : PHY::Modulator(phy)
    , liquid_phy_(phy)
{
}

void LiquidPHY::Modulator::modulate(std::shared_ptr<NetPacket> pkt,
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

    // Fill in the ModPacket
    mpkt.samples = std::move(iqbuf);
    mpkt.pkt = std::move(pkt);
}

void LiquidPHY::Modulator::reconfigure(void)
{
}

LiquidPHY::Demodulator::Demodulator(LiquidPHY &phy)
  : Liquid::Demodulator(phy.soft_header_, phy.soft_payload_)
  , PHY::Demodulator(phy)
  , liquid_phy_(phy)
  , internal_oversample_fact_(1)
{
}

int LiquidPHY::Demodulator::callback(unsigned char *  header_,
                                     int              header_valid_,
                                     int              header_test_,
                                     unsigned char *  payload_,
                                     unsigned int     payload_len_,
                                     int              payload_valid_,
                                     framesyncstats_s stats_)
{
    Header* h = reinterpret_cast<Header*>(header_);

    // Perform test to see if we want to continue demodulating this packets
    if (header_test_) {
        if (header_valid_ && (h->flags & (1 << kBroadcast) || h->nexthop == phy_.getNodeId()))
            return 1;
        else
            return 0;
    }

    // Deal with the demodulated packet
    size_t off = demod_off_;   // Save demodulation offset for use when we log.
    double resamp_fact = internal_oversample_fact_/rate_;

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    demod_off_ += resamp_fact*stats_.end_counter;

    // Create the packet and fill it out
    std::unique_ptr<RadioPacket> pkt;

    if (!header_valid_) {
        if (rc.log_invalid_headers) {
            if (rc.verbose && !rc.debug)
                fprintf(stderr, "HEADER INVALID\n");
            logEvent("PHY: invalid header");
        }

        return 0;
    } else if (!payload_valid_) {
        pkt = std::make_unique<RadioPacket>();

        pkt->setInternalFlag(kInvalidPayload);
        pkt->fromHeader(*h);

        if (h->nexthop == phy_.getNodeId()) {
            if (rc.verbose && !rc.debug)
                fprintf(stderr, "PAYLOAD INVALID\n");
            logEvent("PHY: invalid payload: curhop=%u; nexthop=%u; seq=%u",
                pkt->curhop,
                pkt->nexthop,
                (unsigned) pkt->seq);
        }
    } else {
        pkt = std::make_unique<RadioPacket>(payload_, payload_len_);

        pkt->fromHeader(*h);
        pkt->fromExtendedHeader();
    }

    pkt->evm = stats_.evm;
    pkt->rssi = stats_.rssi;
    pkt->cfo = stats_.cfo;
    pkt->fc = shift_;

    // Calculate sample offsets
    size_t start = off + resamp_fact*stats_.start_counter;
    size_t end = off + resamp_fact*stats_.end_counter;

    pkt->timestamp = demod_start_ + start / phy_.getRXRate();

    if (in_snapshot_)
        liquid_phy_.snapshot_collector_->selfTX(snapshot_off_ + start,
                                                snapshot_off_ + end,
                                                shift_,
                                                phy_.getRXRate()/resamp_fact);

    callback_(std::move(pkt));

    if (logger &&
        logger->getCollectSource(Logger::kRecvPackets) &&
        (header_valid_ || rc.log_invalid_headers)) {
        std::shared_ptr<buffer<std::complex<float>>> buf = nullptr;

        if (logger->getCollectSource(Logger::kRecvData)) {
            buf = std::make_shared<buffer<std::complex<float>>>(stats_.num_framesyms);
            memcpy(buf->data(), stats_.framesyms, stats_.num_framesyms*sizeof(std::complex<float>));
        }

        logger->logRecv(demod_start_,
                        start,
                        end,
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

void LiquidPHY::Demodulator::reset(Clock::time_point timestamp,
                                   size_t off,
                                   double shift,
                                   double rate)
{
    if (pending_reconfigure_.load(std::memory_order_relaxed)) {
        pending_reconfigure_.store(false, std::memory_order_relaxed);
        reconfigure();
    }

    reset();

    rate_ = rate;
    shift_ = shift;
    demod_start_ = timestamp;
    demod_off_ = off;
    in_snapshot_ = false;
    snapshot_off_ = 0;
}

void LiquidPHY::Demodulator::setSnapshotOffset(ssize_t snapshot_off)
{
    if (liquid_phy_.snapshot_collector_) {
        in_snapshot_ = liquid_phy_.snapshot_collector_->active();
        snapshot_off_ = snapshot_off;
    }
}

void LiquidPHY::Demodulator::demodulate(const std::complex<float>* data,
                                        size_t count,
                                        std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    demodulateSamples(data, count);
}

void LiquidPHY::Demodulator::reconfigure(void)
{
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
