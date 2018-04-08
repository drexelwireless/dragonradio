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

int liquidRxCallback(unsigned char *  _header,
                     int              _header_valid,
                     unsigned char *  _payload,
                     unsigned int     _payload_len,
                     int              _payload_valid,
                     framesyncstats_s _stats,
                     void *           _userdata,
                     liquid_float_complex* G,
                     liquid_float_complex* G_hat,
                     unsigned int M);

PHY::PHY(std::shared_ptr<IQTransport> t,
         std::shared_ptr<NET> net,
         double bandwidth,
         size_t min_packet_size,
         unsigned int rx_thread_pool_size)
  : t(t),
    net(net),
    node_id(net->getNodeId()),
    min_packet_size(min_packet_size),
    threads(rx_thread_pool_size),
    thread_joined(rx_thread_pool_size)
{
    // MultiChannel TX/RX requires oversampling by a factor of 2
    t->set_tx_rate(2*bandwidth);
    t->set_rx_rate(2*bandwidth);

    // modem setup (list is for parallel demodulation)
    mctx = std::unique_ptr<multichanneltx>(new multichanneltx(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR));

    for (unsigned int i = 0; i < rx_thread_pool_size; i++) {
        framesync_callback callback[1] = { liquidRxCallback };
        void               *userdata[1] = { this };

        std::unique_ptr<multichannelrx> mcrx(new multichannelrx(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR, userdata, callback));

        mcrx_list.push_back(std::move(mcrx));
    }

    // keep track of demod threads
    for (unsigned int i = 0; i < rx_thread_pool_size; i++)
        thread_joined[i] = true;

    next_thread = 0;
}

PHY::~PHY()
{
}

void PHY::join(void)
{
    for (unsigned int i = 0; i < threads.size(); ++i) {
        if (!thread_joined[i]) {
            threads[i].join();
            thread_joined[i] = true;
        }
    }
}

std::unique_ptr<ModPacket> PHY::modulate(std::unique_ptr<RadioPacket> pkt)
{
    std::unique_ptr<ModPacket> mpkt(new ModPacket);
    PHYHeader                  header;
    size_t                     len = std::max((size_t) pkt->payload_len, min_packet_size);

    memset(&header, 0, sizeof(header));

    header.h.src = node_id;
    header.h.dest = pkt->dest;
    header.h.pkt_id = pkt->packet_id;
    header.h.pkt_len = pkt->payload_len;

    // XXX We assume that the radio packet's buffer has at least min_packet_size
    // bytes available. This is true because we set the size of this buffer to
    // 2000 when we allocate the RadioPacket in NET.cc.
    mctx->UpdateData(0, header.bytes, &(pkt->payload)[0], len, MOD, FEC_INNER, FEC_OUTER);

    const float               scalar = 0.2f;
    const size_t              BUFLEN = 2;
    std::complex<float>       buf[BUFLEN];
    size_t                    nsamples = 0;
    std::unique_ptr<IQBuffer> iqbuf(new IQBuffer(tx_transport_size));

    while (!mctx->IsChannelReadyForData(0)) {
        mctx->GenerateSamples(buf);

        for (unsigned int i = 0; i < BUFLEN; i++)
            (*iqbuf)[nsamples++] = scalar*buf[i];

        if (nsamples == tx_transport_size) {
            iqbuf->resize(nsamples);

            mpkt->appendSamples(std::move(iqbuf));

            iqbuf.reset(new IQBuffer(tx_transport_size));
            nsamples = 0;
        }
    }

    if (nsamples > 0) {
        iqbuf->resize(nsamples);

        mpkt->appendSamples(std::move(iqbuf));
    }

    return mpkt;
}

void run_demod(multichannelrx& mcrx, std::unique_ptr<IQBuffer> usrp_double_buff)
{
    mcrx.Execute(&(*usrp_double_buff)[0], usrp_double_buff->size());
}

void PHY::demodulate(std::unique_ptr<IQBuffer> buf)
{
    if (!thread_joined[next_thread]) {
        threads[next_thread].join();
        thread_joined[next_thread] = true;
    }

    thread_joined[next_thread] = false;
    threads[next_thread] = std::thread(run_demod, std::ref(*(mcrx_list[next_thread])), std::move(buf));
    next_thread = (next_thread + 1) % threads.size();
}

int liquidRxCallback(unsigned char *  _header,
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
    return reinterpret_cast<PHY*>(_userdata)->rxCallback(_header,
                                                         _header_valid,
                                                         _payload,
                                                         _payload_len,
                                                         _payload_valid,
                                                         _stats,
                                                         G,
                                                         G_hat,
                                                         M);
}

int PHY::rxCallback(unsigned char *  _header,
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

    // let first header byte be node id
    // let second header byte be source id
    if (h->dest != net->getNodeId())
        return 0;

    if (h->pkt_len == 0)
        return 1;

    unsigned int num_written = net->sendPacket(_payload, h->pkt_len);

    printf("Written %u bytes (PID %u) from %u", num_written, h->pkt_id, h->src);
    if (M>0)
        printf("|| %u subcarriers || 100th channel sample %.4f+%.4f*1j\n",M,std::real(G[100]),std::imag(G[100]));
    else
        printf("\n");

    return 0;
}
