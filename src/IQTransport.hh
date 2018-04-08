#ifndef IQTRANSPORT_H_
#define IQTRANSPORT_H_

#include <sys/types.h>

#include <deque>

#include "IQBuffer.hh"

class IQTransport
{
public:
    IQTransport() {} ;
    virtual ~IQTransport() {};

    virtual double get_time_now(void) = 0;

    virtual double get_tx_rate(void) = 0;
    virtual void   set_tx_rate(double rate) = 0;
    virtual double get_rx_rate(void) = 0;
    virtual void   set_rx_rate(double rate) = 0;

    /** Transmit samples in queue of IQBuffers in a burst at the given time */
    virtual void burstTX(double when, std::deque<std::unique_ptr<IQBuffer>>& bufs) = 0;

    /** Collect a burst of nsamps samples at time when */
    virtual std::unique_ptr<IQBuffer> burstRX(double when, size_t nsamps) = 0;
};

#endif /* IQTRANSPORT_H_ */
