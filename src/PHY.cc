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

PHY::PHY(std::shared_ptr<FloatIQTransport> t,
         std::shared_ptr<NET> net,
         unsigned int padded_bytes,
         unsigned int rx_thread_pool_size)
  : t(t),
    net(net),
    threads(rx_thread_pool_size),
    thread_joined(rx_thread_pool_size)
{
    this->node_id = net->node_id;
    this->padded_bytes = padded_bytes;
    this->rx_thread_pool_size = rx_thread_pool_size;
    this->tx_transport_size = 512;

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
    PHY* phy = reinterpret_cast<PHY*>(_userdata);

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
    if (_header[0] != phy->net->node_id)
        return 0;

    unsigned int source_id     = _header[1];
    unsigned int packet_length = ((_payload[0] << 8)|(_payload[1]));

    if (packet_length==0)
        return 1;

    unsigned int num_written = phy->net->tt->cwrite((char*)(_payload+phy->padded_bytes),packet_length);
    unsigned int packet_id = (_header[2] << 8) | _header[3];

    printf("Written %u bytes (PID %u) from %u",num_written,packet_id,source_id);
    if (M>0)
        printf("|| %u subcarriers || 100th channel sample %.4f+%.4f*1j\n",M,std::real(G[100]),std::imag(G[100]));
    else
        printf("\n");

    return 0;
}

void PHY::burstRX(double when, size_t nsamps)
{
    const size_t max_samps_per_packet = t->get_max_recv_samps_per_packet();

    unsigned int ii;
    for(ii=0;ii<rx_thread_pool_size;ii++)
    {
        // init counter for samples and allocate double buffer
        size_t uhd_num_delivered_samples = 0;
        std::vector<std::complex<float> >* this_double_buff = new std::vector<std::complex<float> >;

        t->recv_at(when);

        while(uhd_num_delivered_samples<nsamps)
        {
            // create new vector to hold samps for this demod process
            std::vector<std::complex<float> > this_rx_buff(max_samps_per_packet);

            size_t this_num_delivered_samples = t->recv(&this_rx_buff.front(), this_rx_buff.size());

            for(unsigned int kk=0;kk<this_num_delivered_samples;kk++)
            {
                this_double_buff->push_back(this_rx_buff[kk]);
            }

            //this_double_buff.push_back(this_rx_buff);
            uhd_num_delivered_samples += this_num_delivered_samples;
        }

        if(!thread_joined[ii])
        {
            threads[ii].join();
            thread_joined[ii] = true;
        }

        threads[ii] = std::thread(&PHY::run_demod,this,this_double_buff,ii);
        thread_joined[ii] = false;
    }
}

void PHY::run_demod(std::vector<std::complex<float> >* usrp_double_buff,unsigned int thread_idx)
{
    for(std::vector<std::complex<float> >::iterator double_it=usrp_double_buff->begin();double_it!=usrp_double_buff->end();double_it++)
    {
        std::complex<float> usrp_sample = *double_it;
        mcrx_list.at(thread_idx)->Execute(&usrp_sample,1);
    }
    delete usrp_double_buff;
}

void PHY::prepareTXBurst(int npackets)
{
    tx_double_buff.clear();
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
                last_packet = tx_packet->packet_id;
                unsigned char* packet_payload = tx_packet->payload;
                dest_id = tx_packet->destination_id;
                unsigned char* padded_packet = new unsigned char[packet_length+padded_bytes];
                unsigned char header[8];
                memmove(padded_packet+padded_bytes,packet_payload,packet_length);
                padded_packet[0] = (packet_length>>8) & 0xff;
                padded_packet[1] = (packet_length) & 0xff;
                header[0] = dest_id;
                header[1] = node_id;
                header[2] = ((tx_packet->packet_id)>>8) & 0xff;
                header[3] = (tx_packet->packet_id) & 0xff;
                for(unsigned int ii=4;ii<8;ii++)
                {
                    header[ii] = 0&0xff;
                }
                mctx->UpdateData(0,header,padded_packet,padded_bytes+packet_length,LIQUID_MODEM_QPSK,LIQUID_FEC_CONV_V27,LIQUID_FEC_RS_M8);

                // populate usrp buffer
                unsigned int mctx_buffer_length = 2;
                std::complex<float> mctx_buffer[mctx_buffer_length];
                std::vector<std::complex<float> >* usrp_tx_buff = new std::vector<std::complex<float> >(tx_transport_size);
                unsigned int num_generated_samples=0;
                while(!mctx->IsChannelReadyForData(0))
                {
                    mctx->GenerateSamples(mctx_buffer);
                    for(unsigned int jj=0;jj<mctx_buffer_length;jj++)
                    {
                        float scalar = 0.2f;
                        usrp_tx_buff->at(num_generated_samples) = (scalar*mctx_buffer[jj]);
                        num_generated_samples++;
                    }
                    if(num_generated_samples==tx_transport_size)
                    {
                        tx_double_buff.push_back(usrp_tx_buff);
                        usrp_tx_buff = new std::vector<std::complex<float> >(tx_transport_size);
                        num_generated_samples = 0;
                    }
                }
                if(num_generated_samples>0)
                {
                    tx_double_buff.push_back(usrp_tx_buff);
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
    if(tx_double_buff.size()>0)
    {
        t->start_burst();

        for(std::vector<std::vector<std::complex<float> >* >::iterator it=tx_double_buff.begin();it!=tx_double_buff.end();it++)
        {
            // tx that packet (each buffer in the double buff is one packet)
            t->send(when, &((*it)->front()),(*it)->size());
            delete *it;
        }

        // Clear buffer
        t->end_burst();
        t->send(when, NULL, 0);
    }
}
