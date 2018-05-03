#include "Liquid.hh"
#include "Logger.hh"

std::mutex liquid_mutex;

LiquidDemodulator::LiquidDemodulator(std::function<bool(Header&)> predicate) :
    _predicate(predicate),
    _resamp_fact(1)
{
}

LiquidDemodulator::~LiquidDemodulator()
{
}

int LiquidDemodulator::liquid_callback(unsigned char *  _header,
                                       int              _header_valid,
                                       unsigned char *  _payload,
                                       unsigned int     _payload_len,
                                       int              _payload_valid,
                                       framesyncstats_s _stats,
                                       void *           _userdata)
{
    return reinterpret_cast<LiquidDemodulator*>(_userdata)->
        callback(_header,
                 _header_valid,
                 _payload,
                 _payload_len,
                 _payload_valid,
                 _stats);
}

int LiquidDemodulator::callback(unsigned char *  _header,
                                int              _header_valid,
                                unsigned char *  _payload,
                                unsigned int     _payload_len,
                                int              _payload_valid,
                                framesyncstats_s _stats)
{
    Header* h = reinterpret_cast<Header*>(_header);
    size_t  off = _demod_off;   // Save demodulation offset for use when we log.
    bool    incomplete = false; // Is this an incomplete packet?

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    _demod_off += _stats.end_counter;

    if (!_header_valid) {
        printf("HEADER INVALID\n");
        incomplete = true;
    } else if (!_payload_valid) {
        printf("PAYLOAD INVALID\n");
        incomplete = true;
    } else if (!_predicate(*h))
        return 0;
    else if (h->pkt_len == 0)
        return 0;

    if (incomplete)
        _callback(nullptr);
    else {
        auto pkt = std::make_unique<RadioPacket>(_payload, h->pkt_len);

        pkt->src = h->src;
        pkt->dest = h->dest;
        pkt->pkt_id = h->pkt_id;
        pkt->evm = _stats.evm;
        pkt->rssi = _stats.rssi;

        _callback(std::move(pkt));
    }

    if (logger) {
        auto buf = std::make_shared<buffer<std::complex<float>>>(_stats.num_framesyms);
        memcpy(buf->data(), _stats.framesyms, _stats.num_framesyms*sizeof(std::complex<float>));
        logger->logRecv(_demod_start,
                        off + _stats.start_counter,
                        off + _stats.end_counter,
                        _header_valid,
                        _payload_valid,
                        *h,
                        _stats.evm,
                        _stats.rssi,
                        std::move(buf));
    }

    return 0;
}
