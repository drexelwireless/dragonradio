#include "PHY.hh"

int rxCallback(
        unsigned char *  _header,
        int              _header_valid,
        unsigned char *  _payload,
        unsigned int     _payload_len,
        int              _payload_valid,
        framesyncstats_s _stats,
        void *           _userdata,
        liquid_float_complex* G,
        liquid_float_complex* G_hat,
        unsigned int M
    );

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

typedef uint8_t NodeId;
typedef uint16_t PacketId;

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

PHY::PHY(std::shared_ptr<FloatIQTransport> t,
         std::shared_ptr<NET> net,
         unsigned int padded_bytes,
         unsigned int rx_thread_pool_size)
  : node_id(net->node_id),
    padded_bytes(padded_bytes),
    t(t),
    net(net),
    rx_thread_pool_size(rx_thread_pool_size),
    threads(rx_thread_pool_size),
    thread_joined(rx_thread_pool_size)
{
    // modem setup (list is for parallel demodulation)
    mctx = std::unique_ptr<multichanneltx>(new multichanneltx(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR));

    for(unsigned int jj=0;jj<rx_thread_pool_size;jj++)
    {
        framesync_callback callback[1] = { rxCallback };
        void               *userdata[1] = { this };

        std::unique_ptr<multichannelrx> mcrx(new multichannelrx(NUM_CHANNELS, M, CP_LEN, TP_LEN, SUBCAR, userdata, callback));

        mcrx_list.push_back(std::move(mcrx));
    }

    // keep track of demod threads
    unsigned int ii;
    for(ii=0;ii<rx_thread_pool_size;ii++)
    {
        thread_joined[ii] = true;
    }
}

PHY::~PHY()
{
}

int rxCallback(
        unsigned char *  _header,
        int              _header_valid,
        unsigned char *  _payload,
        unsigned int     _payload_len,
        int              _payload_valid,
        framesyncstats_s _stats,
        void *           _userdata,
        liquid_float_complex* G,
        liquid_float_complex* G_hat,
        unsigned int M
        )
{
    PHY*    phy = reinterpret_cast<PHY*>(_userdata);
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
    if (h->dest != phy->net->node_id)
        return 0;

    if (h->pkt_len == 0)
        return 1;

    unsigned int num_written = phy->net->tt->cwrite((char*)(_payload+phy->padded_bytes), h->pkt_len);

    printf("Written %u bytes (PID %u) from %u", num_written, h->pkt_id, h->src);
    if (M>0)
        printf("|| %u subcarriers || 100th channel sample %.4f+%.4f*1j\n",M,std::real(G[100]),std::imag(G[100]));
    else
        printf("\n");

    return 0;
}

void run_demod(multichannelrx& mcrx, std::unique_ptr<IQBuffer> usrp_double_buff)
{
    mcrx.Execute(&(*usrp_double_buff)[0], usrp_double_buff->size());
}

void PHY::burstRX(double when, size_t nsamps)
{
    const size_t max_samps_per_packet = t->get_max_recv_samps_per_packet();

    for (unsigned int i = 0; i < rx_thread_pool_size; i++) {
        // init counter for samples and allocate double buffer
        size_t                    uhd_num_delivered_samples = 0;
        std::unique_ptr<IQBuffer> rx_buf(new IQBuffer);

        t->recv_at(when);

        while (uhd_num_delivered_samples < nsamps) {
            rx_buf->resize(uhd_num_delivered_samples + max_samps_per_packet);

            uhd_num_delivered_samples += t->recv(&(*rx_buf)[uhd_num_delivered_samples], max_samps_per_packet);
        }

        rx_buf->resize(uhd_num_delivered_samples);

        if (!thread_joined[i])
            threads[i].join();

        thread_joined[i] = false;
        threads[i] = std::thread(run_demod, std::ref(*(mcrx_list[i])), std::move(rx_buf));
    }
}

void PHY::prepareTXBurst(int npackets)
{
    tx_buf.clear();
    unsigned int packet_count = 0;
    int last_packet = -1;
    while((packet_count<npackets) && (net->tx_packets.size()>0))
    //for(packet_count=0;packet_count<packets_per_slot;packet_count++)
    {
        printf("Got Packet\n");

        // construct next header and padded payload
        unsigned int packet_length;
        unsigned int dest_id;
        tx_packet_t* tx_packet = net->get_next_packet();
        packet_length = tx_packet->payload_size;
        if(packet_length>0)
        {
            if(last_packet!=(tx_packet->packet_id))
            {
                PHYHeader      header;
                unsigned char* padded_packet = new unsigned char[packet_length+padded_bytes];
                unsigned char* packet_payload = tx_packet->payload;

                last_packet = tx_packet->packet_id;
                dest_id = tx_packet->destination_id;
                memmove(padded_packet+padded_bytes,packet_payload,packet_length);

                memset(&header, 0, sizeof(header));

                header.h.src = node_id;
                header.h.dest = dest_id;
                header.h.pkt_id = tx_packet->packet_id;
                header.h.pkt_len = packet_length;

                mctx->UpdateData(0,header.bytes,padded_packet,padded_bytes+packet_length,MOD,FEC_INNER,FEC_OUTER);

                // populate usrp buffer
                unsigned int mctx_buffer_length = 2;
                std::vector<std::complex<float>> mctx_buffer(mctx_buffer_length);
                std::unique_ptr<IQBuffer> usrp_tx_buff(new IQBuffer(tx_transport_size));
                unsigned int num_generated_samples=0;
                while(!mctx->IsChannelReadyForData(0))
                {
                    mctx->GenerateSamples(&(mctx_buffer[0]));
                    for(unsigned int jj=0;jj<mctx_buffer_length;jj++)
                    {
                        float scalar = 0.2f;
                        usrp_tx_buff->at(num_generated_samples) = (scalar*mctx_buffer[jj]);
                        num_generated_samples++;
                    }
                    if(num_generated_samples==tx_transport_size)
                    {
                        tx_buf.push_back(std::move(usrp_tx_buff));
                        usrp_tx_buff.reset(new IQBuffer(tx_transport_size));
                        num_generated_samples = 0;
                    }
                }
                if(num_generated_samples>0)
                {
                    tx_buf.push_back(std::move(usrp_tx_buff));
                    num_generated_samples = 0;
                }
                delete[] padded_packet;
                delete tx_packet;
            }
        }
    }
}

void PHY::burstTX(double when)
{
    // tx timed burst
    if (tx_buf.size() > 0) {
        t->start_burst();

        for(auto it = tx_buf.begin(); it != tx_buf.end(); it++) {
            if (std::next(it) == tx_buf.end())
                t->end_burst();

            // tx that packet (each buffer in the double buff is one packet)
            t->send(when, &((*it)->front()),(*it)->size());
        }

        // Clear buffer
        tx_buf.clear();
    }
}
