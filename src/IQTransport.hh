#ifndef IQTRANSPORT_H_
#define IQTRANSPORT_H_

#include <sys/types.h>

template <typename T>
class IQTransport
{
public:
    IQTransport() {} ;
    virtual ~IQTransport() {};

    virtual double get_time_now(void) = 0;

    virtual double get_tx_rate(void) = 0;
    virtual double get_rx_rate(void) = 0;

    virtual size_t get_max_send_samps_per_packet(void) = 0;
    virtual size_t get_max_recv_samps_per_packet(void) = 0;

    virtual void   recv_at(double when) = 0;
    virtual size_t recv(const T* buf, size_t count) = 0;

    virtual void   start_burst(void) = 0;
    virtual void   end_burst(void) = 0;
    virtual size_t send(double when, const T* buf, size_t count) = 0;
};

typedef IQTransport<std::complex<float>> FloatIQTransport;

#endif /* IQTRANSPORT_H_ */
