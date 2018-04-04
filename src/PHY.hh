#ifndef PHY_H_
#define PHY_H_

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

class PHY
{
public:
    PHY(std::shared_ptr<FloatIQTransport> t,
        std::shared_ptr<NET> net,
        unsigned int padded_bytes,
        unsigned int rx_thread_pool_size);
    ~PHY();

    void prepareTXBurst(int npackets);
    void burstTX(double when);

    void burstRX(double when, size_t nsamps);

    // other shite
    std::shared_ptr<FloatIQTransport> t;
    std::shared_ptr<NET> net;
    unsigned int node_id;
    unsigned int padded_bytes;
    std::unique_ptr<multichanneltx> mctx;
    std::vector<std::unique_ptr<multichannelrx>> mcrx_list;
    std::vector<std::vector<std::complex<float> >* > tx_double_buff;
    unsigned int tx_transport_size;
    unsigned int rx_thread_pool_size;


    std::vector<std::thread> threads;
    std::vector<bool>        thread_joined;

    void rx_worker(void);
    void run_demod(std::vector<std::complex<float> >* usrp_double_buff,unsigned int thread_idx);
};

#endif /* PHY_H_ */
