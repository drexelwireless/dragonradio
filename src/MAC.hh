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
        void run(void);

        std::shared_ptr<IQTransport> t;
        std::shared_ptr<NET> net;
        std::shared_ptr<PHY> phy;
        ModQueue             modQueue;
        double frame_size;
        double slot_size;
        double pad_size;
        bool continue_running;
        std::thread rx_worker_thread;

        void rx_worker(void);

        void txSlot(double when, size_t maxSamples);
};

#endif /* MAC_H_ */
