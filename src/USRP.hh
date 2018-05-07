#ifndef USRP_H_
#define USRP_H_

#include <deque>
#include <string>
#include <thread>

#include <uhd/usrp/multi_usrp.hpp>

#include "Clock.hh"
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

    /** @brief Get TX rate. */
    double get_tx_rate(void);

    /** @brief Set TX rate. */
    void set_tx_rate(double rate);

    /** @brief Get RX rate. */
    double get_rx_rate(void);

    /** @brief Set RX rate. */
    void set_rx_rate(double rate);

    /** @brief Transmit a burst of IQ buffers at the given time.
     * @param when Time at which to start the burst.
     * @param bufs A list of IQBuf%s to transmit.
     */
    void burstTX(Clock::time_point when, std::list<std::shared_ptr<IQBuf>>& bufs);

    /** @brief Start streaming read */
    void startRXStream(Clock::time_point when);

    /** @brief Stop streaming read */
    void stopRXStream(void);

    /** @brief Receive specified number of samples at the given time
     * @param when The time at which to start receiving.
     * @param The number of samples to receive.
     * @param buf The IQBuf to hold received IQ samples.
     */
    void burstRX(Clock::time_point when, size_t nsamps, IQBuf& buf);

    /** @brief Return the Maximum number of samples we will read at a time
     * during burstRX. */
    size_t getMaxRXSamps(void)
    {
        return _rx_max_samps;
    }

    /** @brief Stop processing data. */
    void stop(void);

private:
    /** @brief Our associated UHD USRP. */
    uhd::usrp::multi_usrp::sptr usrp;

    /** @brief Flag indicating whether or not this is an X310. */
    bool x310;

    /** @brief The UHD TX stream for this USRP. */
    uhd::tx_streamer::sptr tx_stream;

    /** @brief The UHD RX stream for this USRP. */
    uhd::rx_streamer::sptr rx_stream;

    /** @brief Maximum number of samples we will send at a time during burstTX.
     */
    size_t _tx_max_samps;

    /** @brief Maximum number of samples we will read at a time during burstRX.
     */
    size_t _rx_max_samps;

    /** @brief Flag indicating the we should stop processing data. */
    bool _done;

    /** @brief Thread that receives TX errors. */
    std::thread _tx_thread;

    /** @brief Worker that receives TX errors. */
    void _tx_error(void);
};

#endif /* USRP_H_ */
