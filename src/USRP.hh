#ifndef USRP_H_
#define USRP_H_

#include <deque>
#include <string>
#include <uhd/usrp/multi_usrp.hpp>

#include "IQBuffer.hh"

/** @brief A USRP. */
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

    /** @brief Transmit samples in queue of IQBuffers in a burst at the given time */
    void burstTX(uhd::time_spec_t when, std::deque<std::shared_ptr<IQBuf>>& bufs);

    /** @brief Start streaming read */
    void startRXStream(uhd::time_spec_t when);

    /** @brief Stop streaming read */
    void stopRXStream(void);

    /** @brief Receive specified number of samples at the given time
     * @param when The time at which to start receiving.
     * @param The number of samples to receive.
     * @param buf The IQBuf to hold received IQ samples.
     */
    void burstRX(uhd::time_spec_t when, size_t nsamps, IQBuf& buf);

    /** Maximum number of samples we will read at a time during burstRX. */
    static const size_t MAXSAMPS = 2048;

private:
    uhd::usrp::multi_usrp::sptr usrp;
    bool                        x310;
    uhd::tx_metadata_t          tx_md;
    uhd::tx_streamer::sptr      tx_stream;
    uhd::rx_streamer::sptr      rx_stream;

    /** @brief Maximum number of samples in a TX packet. */
    size_t _tx_max_samps;
};

#endif /* USRP_H_ */
