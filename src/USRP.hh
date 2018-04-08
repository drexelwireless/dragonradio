#ifndef USRP_H_
#define USRP_H_

#include <string>
#include <uhd/usrp/multi_usrp.hpp>

#include "IQTransport.hh"

class USRP : public IQTransport
{
public:
    USRP(const std::string& addr,
         bool x310,
         double center_freq,
         const std::string& tx_ant,
         const std::string& rx_ant,
         float tx_gain,
         float rx_gain);
    ~USRP();

    double get_time_now(void);

    double get_tx_rate(void);
    void   set_tx_rate(double rate);
    double get_rx_rate(void);
    void   set_rx_rate(double rate);

    size_t get_max_send_samps_per_packet(void);
    size_t get_max_recv_samps_per_packet(void);

    void   recv_at(double when);
    size_t recv(std::complex<float>* buf, size_t count);

    void   start_burst(void);
    void   end_burst(void);
    size_t send(double when, const std::complex<float>* buf, size_t count);

private:
    uhd::usrp::multi_usrp::sptr usrp;
    bool                        x310;
    uhd::tx_metadata_t          tx_md;
    uhd::tx_streamer::sptr      tx_stream;
    uhd::rx_streamer::sptr      rx_stream;
};

#endif /* USRP_H_ */
