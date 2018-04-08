// DWSL - full radio stack

#ifndef MAC_H_
#define MAC_H_

#include <vector>
#include <complex>
#include <liquid/liquid.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>
#include <thread>
#include <fstream>

#include "ModQueue.hh"
#include "NET.hh"
#include "PHY.hh"
#include "USRP.hh"

class MAC
{
public:
    //functions
    MAC(std::shared_ptr<IQTransport> t,
        std::shared_ptr<NET> net,
        std::shared_ptr<PHY> phy,
        double frame_size,
        double pad_size);
    ~MAC();

    void join(void);

private:
    std::shared_ptr<IQTransport> t;
    std::shared_ptr<NET>         net;
    std::shared_ptr<PHY>         phy;
    ModQueue                     modQueue;

    double frame_size;
    double slot_size;
    double pad_size;

    /** Flag indicating if we should stop processing packets */
    bool done;

    /** Thread running rxWorker */
    std::thread rxThread;

    /** Worker receiving packets */
    void rxWorker(void);

    /** Thread running txWorker */
    std::thread txThread;

    /** Worker transmitting packets */
    void txWorker(void);

    /** Transmit one slot's worth of samples */
    void txSlot(double when, size_t maxSamples);
};

#endif /* MAC_H_ */
