// DWSL - full radio stack

#ifndef MACPHY_H_
#define MACPHY_H_

#include <vector>
#include <complex>
#include <liquid/liquid.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <thread>
#include <fstream>

#include "NET.hh"
#include "multichannelrx.h"
#include "multichanneltx.h"
#include "USRP.hh"

class MACPHY
{
    public:
        //functions
        MACPHY(std::shared_ptr<FloatIQTransport> t,
               std::shared_ptr<NET> net,
               unsigned int padded_bytes,
               double frame_size,
               unsigned int rx_thread_pool_size,
               float pad_size,
               unsigned int packets_per_slot);
        ~MACPHY();
        void TX_TDMA_OFDM();
        void readyOFDMBuffer();

        // other shite
        std::shared_ptr<FloatIQTransport> t;
        std::shared_ptr<NET> net;
        unsigned int node_id;
        double frame_size;
        double slot_size;
        unsigned int padded_bytes;
        std::vector<multichannelrx*>* mcrx_list;
        std::vector<std::vector<std::complex<float> >* > tx_double_buff;
        unsigned int tx_transport_size;
        multichanneltx* mctx;
        bool continue_running;
        unsigned int rx_thread_pool_size;
        float pad_size;
        unsigned int packets_per_slot;
        std::thread rx_worker_thread;

        void rx_worker(void);
        void run_demod(std::vector<std::complex<float> >* usrp_double_buff,unsigned int thread_idx);
};

#endif
