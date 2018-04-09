#ifndef USRP_H_
#define USRP_H_

#include <deque>
#include <string>
#include <uhd/usrp/multi_usrp.hpp>

#include "IQBuffer.hh"

class USRP
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

    uhd::time_spec_t get_time_now(void);

    double get_tx_rate(void);
    void   set_tx_rate(double rate);
    double get_rx_rate(void);
    void   set_rx_rate(double rate);

    void burstTX(uhd::time_spec_t when, std::deque<std::unique_ptr<IQBuf>>& bufs);

    std::unique_ptr<IQBuf> burstRX(uhd::time_spec_t when, size_t nsamps);

private:
    uhd::usrp::multi_usrp::sptr usrp;
    bool                        x310;
    uhd::tx_metadata_t          tx_md;
    uhd::tx_streamer::sptr      tx_stream;
    uhd::rx_streamer::sptr      rx_stream;
};

#endif /* USRP_H_ */
