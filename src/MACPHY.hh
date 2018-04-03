// DWSL - full radio stack

#ifndef MACPHY_H_
#define MACPHY_H_

#include <vector>
#include <complex>
#include <liquid/liquid.h>
#include <stdio.h>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/msg.hpp>
#include <math.h>
#include <sys/time.h>
#include <thread>
#include <fstream>

#include "NET.hh"
#include "multichannelrx.h"
#include "multichanneltx.h"

class MACPHY
{
    public:
        //functions
        MACPHY(const char* addr,
               NET* net,
               double center_freq,
               double bandwidth,
               unsigned int padded_bytes,
               float tx_gain,
               float rx_gain,
               double frame_size,
               unsigned int rx_thread_pool_size,
               float pad_size,
               unsigned int packets_per_slot);
        ~MACPHY();
        void TX_TDMA_OFDM();
        void readyOFDMBuffer();

        // other shite
        unsigned int num_nodes_in_net;
        unsigned char* nodes_in_net;
        unsigned int node_id;
        double frame_size;
        double slot_size;
        NET* net;
        unsigned int padded_bytes;
        uhd::usrp::multi_usrp::sptr usrp;
        uhd::rx_streamer::sptr rx_stream;
        uhd::tx_streamer::sptr tx_stream;
        std::vector<multichannelrx*>* mcrx_list;
        std::vector<std::vector<std::complex<float> >* > tx_double_buff;
        unsigned int tx_transport_size;
        multichanneltx* mctx;
        bool continue_running;
        unsigned int rx_thread_pool_size;
        float pad_size;
        unsigned int packets_per_slot;
        long unsigned int sim_burst_id;
        std::thread rx_worker_thread;

        void rx_worker(void);
        void run_demod(std::vector<std::complex<float> >* usrp_double_buff,unsigned int thread_idx);
};

#endif
