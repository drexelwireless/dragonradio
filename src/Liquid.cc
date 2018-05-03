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

    if (logger) {
        auto buf = std::make_shared<buffer<std::complex<float>>>(_stats.num_framesyms);
        memcpy(buf->data(), _stats.framesyms, _stats.num_framesyms*sizeof(std::complex<float>));
        logger->logRecv(_demod_start,
                        _header_valid,
                        _payload_valid,
                        *h,
                        _demod_off + _resamp_fact*_stats.start_counter,
                        _demod_off + _resamp_fact*_stats.end_counter,
                        std::move(buf));
    }

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    _demod_off += _resamp_fact*_stats.end_counter;

    if (!_header_valid) {
        printf("HEADER INVALID\n");
        _callback(nullptr);
        return 0;
    }

    if (!_payload_valid) {
        printf("PAYLOAD INVALID\n");
        _callback(nullptr);
        return 0;
    }

    if (!_predicate(*h)) {
        _callback(nullptr);
        return 0;
    }

    if (h->pkt_len == 0) {
        _callback(nullptr);
        return 0;
    }

    auto pkt = std::make_unique<RadioPacket>(_payload, h->pkt_len);

    pkt->src = h->src;
    pkt->dest = h->dest;
    pkt->pkt_id = h->pkt_id;

    _callback(std::move(pkt));

    return 0;
}
