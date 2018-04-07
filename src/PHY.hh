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

#include "ModPacket.hh"
#include "NET.hh"
#include "Node.hh"
#include "multichannelrx.h"
#include "multichanneltx.h"
#include "USRP.hh"

class PHY
{
public:
    PHY(std::shared_ptr<FloatIQTransport> t,
        std::shared_ptr<NET> net,
        size_t min_packet_size,
        unsigned int rx_thread_pool_size);
    ~PHY();

    std::unique_ptr<ModPacket> modPkt(std::unique_ptr<RadioPacket> pkt);

    void prepareTXBurst(unsigned int npackets);
    void burstTX(double when);

    void burstRX(double when, size_t nsamps);

    // other shite
    NodeId node_id;
    size_t min_packet_size;
    std::shared_ptr<FloatIQTransport> t;
    std::shared_ptr<NET> net;
    std::unique_ptr<multichanneltx> mctx;
    std::vector<std::unique_ptr<multichannelrx>> mcrx_list;
    /** Buffer holding modulated data */
    std::vector<std::unique_ptr<IQBuffer>> tx_buf;
    unsigned int rx_thread_pool_size;

    std::vector<std::thread> threads;
    std::vector<bool>        thread_joined;

    void rx_worker(void);
};

#endif /* PHY_H_ */
