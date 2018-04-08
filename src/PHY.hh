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
    PHY(std::shared_ptr<IQTransport> t,
        std::shared_ptr<NET> net,
        double bandwidth,
        size_t min_packet_size,
        unsigned int rx_thread_pool_size);
    ~PHY();

    void join(void);

    std::unique_ptr<ModPacket> modulate(std::unique_ptr<RadioPacket> pkt);

    void demodulate(std::unique_ptr<IQBuffer> buf);

    int rxCallback(unsigned char *  _header,
                   int              _header_valid,
                   unsigned char *  _payload,
                   unsigned int     _payload_len,
                   int              _payload_valid,
                   framesyncstats_s _stats,
                   liquid_float_complex* G,
                   liquid_float_complex* G_hat,
                   unsigned int M);

private:
    std::shared_ptr<IQTransport> t;
    std::shared_ptr<NET>         net;

    NodeId node_id;
    size_t min_packet_size;

    std::unique_ptr<multichanneltx>              mctx;
    std::vector<std::unique_ptr<multichannelrx>> mcrx_list;

    std::vector<std::thread> threads;
    std::vector<bool>        thread_joined;
    int                      next_thread;
};

#endif /* PHY_H_ */
