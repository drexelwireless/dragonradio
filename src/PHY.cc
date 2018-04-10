#include "PHY.hh"

/** Number of channels */
const unsigned int NUM_CHANNELS = 1;

/** Number of OFDM subcarriers */
const unsigned int M = 480;

/** OFDM cyclic prefix length */
const unsigned int CP_LEN = 6;

/** OFDM taper prefix length */
const unsigned int TP_LEN = 4;

/** OFDM subcarrier allocation */
unsigned char *SUBCAR = nullptr;

/** Number of IQ samples per IQ buffer */
const unsigned int tx_transport_size = 512;

/** Modulation */
const int MOD = LIQUID_MODEM_QPSK;

/** Inner FEC */
const int FEC_INNER = LIQUID_FEC_CONV_V27;

/** Outer FEC */
const int FEC_OUTER = LIQUID_FEC_RS_M8;

struct Header {
    NodeId   src;
    NodeId   dest;
    PacketId pkt_id;
    uint16_t pkt_len;
};

union PHYHeader {
    Header        h;
    unsigned char bytes[8];
};

/** Mutex protecting access to the multichanneltxrx code, which is not
 *  re-rentrant!
 */
static std::mutex mctxrx_mutex;

Modulator::Modulator(size_t minPacketSize) :
    minPacketSize(minPacketSize)
{
    std::lock_guard<std::mutex> lck(mctxrx_mutex);

    // modem setup (list is for parallel demodulation)
    mctx = std::make_unique<multichanneltx>(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR);
}

Modulator::~Modulator()
{
}

std::unique_ptr<ModPacket> Modulator::modulate(std::unique_ptr<NetPacket> pkt)
{
    auto      mpkt = std::make_unique<ModPacket>();
    PHYHeader header;
    size_t    len = std::max((size_t) pkt->payload_len, minPacketSize);

    memset(&header, 0, sizeof(header));

    header.h.src = pkt->src;
    header.h.dest = pkt->dest;
    header.h.pkt_id = pkt->pkt_id;
    header.h.pkt_len = pkt->payload_len;

    // XXX We assume that the radio packet's buffer has at least minPacketSize
    // bytes available. This is true because we set the size of this buffer to
    // 2000 when we allocate the NetPacket in NET.cc.
    mctx->UpdateData(0, header.bytes, &(pkt->payload)[0], len, MOD, FEC_INNER, FEC_OUTER);

    const float         scalar = 0.2f;
    const size_t        BUFLEN = 2;
    std::complex<float> buf[BUFLEN];
    size_t              nsamples = 0;
    auto                iqbuf = std::make_unique<IQBuf>(tx_transport_size);

    while (!mctx->IsChannelReadyForData(0)) {
        mctx->GenerateSamples(buf);

        for (unsigned int i = 0; i < BUFLEN; i++)
            (*iqbuf)[nsamples++] = scalar*buf[i];

        if (nsamples == tx_transport_size) {
            mpkt->appendSamples(std::move(iqbuf));

            iqbuf = std::make_unique<IQBuf>(tx_transport_size);
            nsamples = 0;
        }
    }

    if (nsamples > 0) {
        iqbuf->resize(nsamples);

        mpkt->appendSamples(std::move(iqbuf));
    }

    return mpkt;
}

Demodulator::Demodulator(std::shared_ptr<NET> net) :
    net(net)
{
    std::lock_guard<std::mutex> lck(mctxrx_mutex);

    // modem setup (list is for parallel demodulation)
    framesync_callback callback[1] = { &Demodulator::liquidRxCallback };
    void               *userdata[1] = { this };

    mcrx = std::make_unique<multichannelrx>(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR, userdata, callback);
}

Demodulator::~Demodulator()
{
}

void Demodulator::demodulate(std::unique_ptr<IQQueue> buf, std::queue<std::unique_ptr<RadioPacket>>& q)
{
    pkts = &q;

    mcrx->Reset();

    for (auto it = buf->begin(); it != buf->end(); ++it)
        mcrx->Execute(&(*it)[0], it->size());
}

int Demodulator::liquidRxCallback(unsigned char *  _header,
                                  int              _header_valid,
                                  unsigned char *  _payload,
                                  unsigned int     _payload_len,
                                  int              _payload_valid,
                                  framesyncstats_s _stats,
                                  void *           _userdata,
                                  liquid_float_complex* G,
                                  liquid_float_complex* G_hat,
                                  unsigned int M)
{
    return reinterpret_cast<Demodulator*>(_userdata)->rxCallback(_header,
                                                                 _header_valid,
                                                                 _payload,
                                                                 _payload_len,
                                                                 _payload_valid,
                                                                 _stats,
                                                                 G,
                                                                 G_hat,
                                                                 M);
}

int Demodulator::rxCallback(unsigned char *  _header,
                            int              _header_valid,
                            unsigned char *  _payload,
                            unsigned int     _payload_len,
                            int              _payload_valid,
                            framesyncstats_s _stats,
                            liquid_float_complex* G,
                            liquid_float_complex* G_hat,
                            unsigned int M)
{
    Header* h = reinterpret_cast<Header*>(_header);

    if (!_header_valid) {
        printf("HEADER INVALID\n");
        return 0;
    }

    if (!_payload_valid) {
        printf("PAYLOAD INVALID\n");
        return 0;
    }

    if (h->dest != net->getNodeId())
        return 0;

    if (h->pkt_len == 0)
        return 1;

    auto pkt = std::make_unique<RadioPacket>(_payload, h->pkt_len);

    pkt->src = h->src;
    pkt->dest = h->dest;
    pkt->pkt_id = h->pkt_id;

    pkts->push(std::move(pkt));

    printf("Written %u bytes (PID %u) from %u", h->pkt_len, h->pkt_id, h->src);
    if (M>0)
        printf("|| %u subcarriers || 100th channel sample %.4f+%.4f*1j\n",M,std::real(G[100]),std::imag(G[100]));
    else
        printf("\n");

    return 0;
}
