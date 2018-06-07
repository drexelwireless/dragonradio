#include "Logger.hh"
#include "RadioConfig.hh"
#include "phy/Liquid.hh"

std::mutex liquid_mutex;

LiquidDemodulator::LiquidDemodulator() :
    resamp_fact_(1)
{
}

LiquidDemodulator::~LiquidDemodulator()
{
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
    bool    incomplete = false; // Is this an incomplete packet?

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    demod_off_ += stats_.end_counter;

    if (!header_valid_) {
        if (rc.verbose)
            fprintf(stderr, "HEADER INVALID\n");
        incomplete = true;
    } else if (!payload_valid_) {
        if (rc.verbose)
            fprintf(stderr, "PAYLOAD INVALID\n");
        incomplete = true;
    }

    if (incomplete)
        callback_(nullptr);
    else {
        auto pkt = std::make_unique<RadioPacket>(payload_, payload_len_);

        pkt->fromHeader(*h);

        pkt->evm = stats_.evm;
        pkt->rssi = stats_.rssi;
        pkt->cfo = stats_.cfo;

        callback_(std::move(pkt));
    }

    if (logger && logger->getCollectSource(Logger::kRecvPackets)) {
        auto buf = std::make_shared<buffer<std::complex<float>>>(stats_.num_framesyms);
        memcpy(buf->data(), stats_.framesyms, stats_.num_framesyms*sizeof(std::complex<float>));
        logger->logRecv(demod_start_,
                        off + stats_.start_counter,
                        off + stats_.end_counter,
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
                        std::move(buf));
    }

    return 0;
}
