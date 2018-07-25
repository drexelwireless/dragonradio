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
    enum DeviceType {
        kUSRPN210,
        kUSRPX310,
        kUSRPUnknown
    };

    USRP(const std::string& addr,
         double freq,
         const std::string& tx_ant,
         const std::string& rx_ant,
         float tx_gain,
         float rx_gain);
    ~USRP();

    USRP() = delete;
    USRP(const USRP&) = delete;
    USRP(USRP&&) = delete;

    USRP& operator=(const USRP&) = delete;
    USRP& operator=(USRP&&) = delete;

    /** @brief Get type of this device. */
    DeviceType getDeviceType(void);

    /** @brief Get TX frequency. */
    double getTXFrequency(void);

    /** @brief Set TX frequency.
     * @param freq The center frequency
     */
    void setTXFrequency(double freq);

    /** @brief Get RX frequency. */
    double getRXFrequency(void);

    /** @brief Set RX frequency.
     * @param freq The center frequency
     */
    void setRXFrequency(double freq);

    /** @brief Get TX rate. */
    double getTXRate(void);

    /** @brief Set TX rate. */
    void setTXRate(double rate);

    /** @brief Get RX rate. */
    double getRXRate(void);

    /** @brief Set RX rate. */
    void setRXRate(double rate);

    /** @brief Get TX gain (dB). */
    double getTXGain(void);

    /** @brief Set TX gain (dB). */
    void setTXGain(float db);

    /** @brief Get RX gain (dB). */
    double getRXGain(void);

    /** @brief Set RX gain (dB). */
    void setRXGain(float db);

    /** @brief Transmit a burst of IQ buffers at the given time.
     * @param when Time at which to start the burst.
     * @param bufs A list of IQBuf%s to transmit.
     */
    void burstTX(MonoClock::time_point when, std::list<std::shared_ptr<IQBuf>>& bufs);

    /** @brief Start streaming read */
    void startRXStream(MonoClock::time_point when);

    /** @brief Stop streaming read */
    void stopRXStream(void);

    /** @brief Receive specified number of samples at the given time
     * @param when The time at which to start receiving.
     * @param The number of samples to receive.
     * @param buf The IQBuf to hold received IQ samples.
     * @returns Returns true if the burst was successfully received, false
     * otherwise.
     */
    bool burstRX(MonoClock::time_point when, size_t nsamps, IQBuf& buf);

    /** @brief Return the maximum number of samples we will read at a time
     * during burstRX.
     */
    size_t getMaxRXSamps(void)
    {
        return rx_max_samps_;
    }

    /** @brief Set the maximum number of samples we will read at a time
     * during burstRX.
     */
    void setMaxRXSamps(size_t count)
    {
        rx_max_samps_ = count;
    }

    /** @brief Return the maximum number of samples we will write at a time
     * during burstTX.
     */
    size_t getMaxTXSamps(void)
    {
        return tx_max_samps_;
    }

    /** @brief Set the maximum number of samples we will write at a time
     * during burstTX.
     */
    void setMaxTXSamps(size_t count)
    {
        tx_max_samps_ = count;
    }

    /** @brief Stop processing data. */
    void stop(void);

private:
    /** @brief Our associated UHD USRP. */
    uhd::usrp::multi_usrp::sptr usrp_;

    /** @brief The DeviceType of the main device */
    DeviceType device_type_;

    /** @brief The UHD TX stream for this USRP. */
    uhd::tx_streamer::sptr tx_stream_;

    /** @brief The UHD RX stream for this USRP. */
    uhd::rx_streamer::sptr rx_stream_;

    /** @brief Maximum number of samples we will send at a time during burstTX.
     */
    size_t tx_max_samps_;

    /** @brief Maximum number of samples we will read at a time during burstRX.
     */
    size_t rx_max_samps_;

    /** @brief Flag indicating the we should stop processing data. */
    bool done_;

    /** @brief Thread that receives TX errors. */
    std::thread tx_error_thread_;

    /** @brief Determine the type of the main device. */
    void determineDeviceType(void);

    /** @brief Worker that receives TX errors. */
    void txErrorWorker(void);
};

#endif /* USRP_H_ */
