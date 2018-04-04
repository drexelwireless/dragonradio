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

#include "NET.hh"
#include "PHY.hh"
#include "USRP.hh"

class MAC
{
    public:
        //functions
        MAC(std::shared_ptr<FloatIQTransport> t,
            std::shared_ptr<NET> net,
            std::shared_ptr<PHY> phy,
            double frame_size,
            float pad_size,
            unsigned int packets_per_slot);
        ~MAC();
        void run(void);

        // other shite
        std::shared_ptr<FloatIQTransport> t;
        std::shared_ptr<NET> net;
        std::shared_ptr<PHY> phy;
        double frame_size;
        double slot_size;
        bool continue_running;
        float pad_size;
        unsigned int packets_per_slot;
        std::thread rx_worker_thread;

        void rx_worker(void);
};

#endif /* MAC_H_ */
