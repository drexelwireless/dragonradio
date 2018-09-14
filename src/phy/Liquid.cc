#include "Logger.hh"
#include "RadioConfig.hh"
#include "WorkQueue.hh"
#include "phy/Liquid.hh"

// Initial modulation buffer size
const size_t kInitialModbufSize = 16384;

// Stop-band attenuation for resamplers
const float kStopBandAttenuationDb = 60.0f;

// Maximum number of samples in upsample buffer. All current liquid modulation
// schemes produce 2 samples at a time, and we normally upsample by at most 8.
// The difficulty is that different modulation schemes operate at different
// "chunk" sizes, i.e., flexframe outputs 2 samples at a time, whereas OFDM
// outputs one frame at a time.
const size_t kMaxUpsampleSize = 1024;

std::mutex liquid_mutex;

union PHYHeader {
    Header h;
    // FLEXFRAME_H_USER in liquid.internal.h. This is the largest header of any
    // of the liquid PHY implementations.
    unsigned char bytes[14];
};

LiquidPHY::LiquidPHY(const MCS &header_mcs,
                     bool soft_header,
                     bool soft_payload,
                     size_t min_packet_size)
  : min_packet_size(min_packet_size)
  , header_mcs_(header_mcs)
  , soft_header_(soft_header)
  , soft_payload_(soft_payload)
{
}

LiquidPHY::LiquidPHY()
  : soft_header_(false)
  , soft_payload_(false)
{
}

LiquidPHY::~LiquidPHY()
{
}

LiquidModulator::LiquidModulator(LiquidPHY &phy)
    : Modulator(phy)
    , liquid_phy_(phy)
{
    upsamp_rate_ = phy.getTXRateOversample()/phy.getMinTXRateOversample();
    upsamp_ = msresamp_crcf_create(upsamp_rate_, kStopBandAttenuationDb);
}

LiquidModulator::~LiquidModulator()
{
    msresamp_crcf_destroy(upsamp_);
}

void LiquidModulator::modulate(ModPacket &mpkt, std::shared_ptr<NetPacket> pkt)
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
    // Number of samples written pre-upsampling
    size_t nw_preup;
    // Number of samples written
    unsigned int nw;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;
    // Modulated output buffer
    std::complex<float> modbuf[kMaxUpsampleSize];

    while (!last_symbol) {
        if (upsamp_rate_ == 1.0) {
            last_symbol = modulateSamples(&(*iqbuf)[nsamples], nw_preup);
            nw = nw_preup;
        } else {
            last_symbol = modulateSamples(modbuf, nw_preup);

            msresamp_crcf_execute(upsamp_,
                                  modbuf,
                                  nw_preup,
                                  &(*iqbuf)[nsamples],
                                  &nw);
        }

        // Apply soft gain. Note that this is where nsamples is incremented.
        for (unsigned int i = 0; i < nw; i++)
            (*iqbuf)[nsamples++] *= g;

        // If we can't add another nw samples to the current IQ buffer, resize
        // it.
        if (nsamples + kMaxUpsampleSize > iqbuf->size())
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

LiquidDemodulator::LiquidDemodulator(LiquidPHY &phy)
  : Demodulator(phy)
  , liquid_phy_(phy)
  , internal_oversample_fact_(1)
{
    downsamp_rate_ = phy.getRXRateOversample()/phy.getMinRXRateOversample();
    downsamp_ = msresamp_crcf_create(1.0/downsamp_rate_, kStopBandAttenuationDb);
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
    double  resamp_fact = internal_oversample_fact_*liquid_phy_.getRXRateOversample()/liquid_phy_.getMinRXRateOversample();

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    demod_off_ += resamp_fact*stats_.end_counter;

    // Create the packet and fill it out
    std::unique_ptr<RadioPacket> pkt;

    if (!header_valid_) {
        if (rc.verbose)
            fprintf(stderr, "HEADER INVALID\n");
        logEvent("PHY: invalid header");

        pkt = std::make_unique<RadioPacket>();

        pkt->setInternalFlag(kInvalidHeader);
    } else if (!payload_valid_) {
        if (rc.verbose)
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
                                   std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    callback_ = callback;

    if (downsamp_rate_ == 1.0) {
        demodulateSamples(data, count);
    } else {
        std::unique_ptr<std::complex<float>[]> downsampbuf(new std::complex<float>[count]);
        unsigned int                           nw;

        msresamp_crcf_execute(downsamp_, data, count, downsampbuf.get(), &nw);

        demodulateSamples(downsampbuf.get(), nw);
    }
}
