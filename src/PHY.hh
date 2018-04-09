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
#include "SafeQueue.hh"
#include "USRP.hh"

using DemodBuffer = std::deque<std::shared_ptr<IQBuffer>>;

class PHY
{
public:
    PHY(std::shared_ptr<USRP> usrp,
        std::shared_ptr<NET> net,
        double bandwidth,
        size_t minPacketSize,
        unsigned int rxThreadPoolSize);
    ~PHY();

    void join(void);

    std::unique_ptr<ModPacket> modulate(std::unique_ptr<RadioPacket> pkt);

    void demodulate(std::unique_ptr<DemodBuffer> buf);

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
    std::shared_ptr<USRP> usrp;
    std::shared_ptr<NET>  net;

    NodeId nodeId;
    size_t minPacketSize;

    std::unique_ptr<multichanneltx>              mctx;
    std::vector<std::unique_ptr<multichannelrx>> mcrxs;

    /** Flag indicating if we should stop processing packets */
    bool done;

    /** Next thread to hand work to */
    int nextThread;

    /** Demodulation threads */
    std::vector<std::thread> threads;

    /** Demodulation threads' queues holding data to demodulate */
    std::vector<SafeQueue<std::unique_ptr<DemodBuffer>>> threadQueues;

    /** Demodulation worker */
    void demodWorker(multichannelrx& mcrx, SafeQueue<std::unique_ptr<DemodBuffer>>& q);
};

#endif /* PHY_H_ */
