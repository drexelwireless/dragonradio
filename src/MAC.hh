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
    MAC(std::shared_ptr<USRP> usrp,
        std::shared_ptr<NET> net,
        std::shared_ptr<PHY> phy,
        double frame_size,
        double guard_size);
    ~MAC();

    void stop(void);

private:
    std::shared_ptr<USRP> usrp;
    std::shared_ptr<NET>  net;
    std::shared_ptr<PHY>  phy;
    ModQueue              modQueue;

    /** Length of TDMA frame (sec) */
    double frame_size;

    /** Length of a single TDMA slot, *including* guard (sec) */
    double slot_size;

    /** Length of inter-slot guard (sec) */
    double guard_size;

    /** Length of previous slot's samples we try to demod in the current slot (sec) */
    double slop_size;

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
    void txSlot(uhd::time_spec_t when, size_t maxSamples);
};

#endif /* MAC_H_ */
