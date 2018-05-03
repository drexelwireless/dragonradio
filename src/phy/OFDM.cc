#include <liquid/liquid.h>

#include "Liquid.hh"
#include "Logger.hh"
#include "OFDM.hh"

union PHYHeader {
    Header        h;
    // FLEXFRAME_H_USER in liquid.internal.h
    unsigned char bytes[14];
};

OFDM::Modulator::Modulator(OFDM& phy) :
    _phy(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    ofdmflexframegenprops_init_default(&_fgprops);
    _fg = ofdmflexframegen_create(_phy._M,
                                  _phy._cp_len,
                                  _phy._taper_len,
                                  _phy._p,
                                  &_fgprops);
}

OFDM::Modulator::~Modulator()
{
    ofdmflexframegen_destroy(_fg);
}

void OFDM::Modulator::print(void)
{
    ofdmflexframegen_print(_fg);
}

void OFDM::Modulator::update_props(NetPacket& pkt)
{
    if (_fgprops.check != pkt.check ||
        _fgprops.fec0 != pkt.fec0 ||
        _fgprops.fec1 != pkt.fec1 ||
        _fgprops.mod_scheme != pkt.ms) {
        _fgprops.check = pkt.check;
        _fgprops.fec0 = pkt.fec0;
        _fgprops.fec1 = pkt.fec1;
        _fgprops.mod_scheme = pkt.ms;

        ofdmflexframegen_setprops(_fg, &_fgprops);
    }
}

// Initial sample buffer size
const size_t MODBUF_SIZE = 16384;

std::unique_ptr<ModPacket> OFDM::Modulator::modulate(std::unique_ptr<NetPacket> pkt)
{
    PHYHeader header;

    memset(&header, 0, sizeof(header));

    header.h.src = pkt->src;
    header.h.dest = pkt->dest;
    header.h.pkt_id = pkt->pkt_id;
    header.h.pkt_len = pkt->size();

    pkt->resize(std::max((size_t) pkt->size(), _phy._minPacketSize));

    update_props(*pkt);
    ofdmflexframegen_reset(_fg);
    ofdmflexframegen_assemble(_fg, header.bytes, pkt->data(), pkt->size());

    // Buffer holding generated IQ samples
    auto iqbuf = std::make_unique<IQBuf>(MODBUF_SIZE);
    // Number of symbols generated
    const size_t NGEN = _phy._M + _phy._cp_len;
    // Number of generated samples in the buffer
    size_t nsamples = 0;
    // Local copy of gain
    const float g = pkt->g;
    // Flag indicating when we've reached the last symbol
    bool last_symbol = false;

    while (!last_symbol) {
        last_symbol = ofdmflexframegen_writesymbol(_fg,
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

    // Construct and return the ModPacket
    auto mpkt = std::make_unique<ModPacket>();

    mpkt->samples = std::move(iqbuf);
    mpkt->pkt = std::move(pkt);

    return mpkt;
}

OFDM::Demodulator::Demodulator(OFDM& phy) :
  _phy(phy)
{
    std::lock_guard<std::mutex> lck(liquid_mutex);

    _fs = ofdmflexframesync_create(_phy._M,
                                   _phy._cp_len,
                                   _phy._taper_len,
                                   _phy._p,
                                   &Demodulator::liquid_callback,
                                   this);
}

OFDM::Demodulator::~Demodulator()
{
    ofdmflexframesync_destroy(_fs);
}

void OFDM::Demodulator::print(void)
{
    ofdmflexframesync_print(_fs);
}

void OFDM::Demodulator::reset(Clock::time_point timestamp, size_t off)
{
    ofdmflexframesync_reset(_fs);

    _demod_start = timestamp;
    _demod_off = off;
}

void OFDM::Demodulator::demodulate(std::complex<float>* data,
                                   size_t count,
                                   std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    _callback = callback;

    ofdmflexframesync_execute(_fs, reinterpret_cast<liquid_float_complex*>(data), count);
}

int OFDM::Demodulator::liquid_callback(unsigned char *  _header,
                                       int              _header_valid,
                                       unsigned char *  _payload,
                                       unsigned int     _payload_len,
                                       int              _payload_valid,
                                       framesyncstats_s _stats,
                                       void *           _userdata)
{
    reinterpret_cast<OFDM::Demodulator*>(_userdata)->
        callback(_header,
                 _header_valid,
                 _payload,
                 _payload_len,
                 _payload_valid,
                 _stats);
    // The ofdmflexframsync code doesn't actually use the callback's return
    // value for anything!
    return 0;
}

void OFDM::Demodulator::callback(unsigned char *  _header,
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
                        _demod_off + _stats.start_counter,
                        _demod_off + _stats.end_counter,
                        std::move(buf));
    }

    // Update demodulation offset. The framesync object is reset after the
    // callback is called, which sets its internal counters to 0.
    _demod_off += _stats.end_counter;

    if (!_header_valid) {
        printf("HEADER INVALID\n");
        _callback(nullptr);
        return;
    }

    if (!_payload_valid) {
        printf("PAYLOAD INVALID\n");
        _callback(nullptr);
        return;
    }

    if (!_phy._net->wantPacket(h->dest)) {
        _callback(nullptr);
        return;
    }

    if (h->pkt_len == 0) {
        _callback(nullptr);
        return;
    }

    auto pkt = std::make_unique<RadioPacket>(_payload, h->pkt_len);

    pkt->src = h->src;
    pkt->dest = h->dest;
    pkt->pkt_id = h->pkt_id;

    _callback(std::move(pkt));
}

std::unique_ptr<PHY::Demodulator> OFDM::make_demodulator(void)
{
    return std::unique_ptr<PHY::Demodulator>(static_cast<PHY::Demodulator*>(new Demodulator(*this)));
}

std::unique_ptr<PHY::Modulator> OFDM::make_modulator(void)
{
    return std::unique_ptr<PHY::Modulator>(static_cast<PHY::Modulator*>(new Modulator(*this)));
}
