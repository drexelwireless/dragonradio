// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef USRP_H_
#define USRP_H_

#include <atomic>
#include <deque>
#include <string>
#include <thread>

#include <uhd/usrp/multi_usrp.hpp>

#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "IQBuffer.hh"
#include "Logger.hh"

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
         const std::optional<std::string>& tx_subdev,
         const std::optional<std::string>& rx_subdev,
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
    DeviceType getDeviceType(void)
    {
        return device_type_;
    }

    /** @brief Get clock sources. */
    std::vector<std::string> getClockSources(const size_t mboard = 0)
    {
        return usrp_->get_clock_sources(mboard);
    }

    /** @brief Get clock source. */
    std::string getClockSource(const size_t mboard = 0)
    {
        return usrp_->get_clock_source(mboard);
    }

    /** @brief Set clock source. */
    void setClockSource(const std::string clock_source,
                        const size_t mboard = uhd::usrp::multi_usrp::ALL_MBOARDS)
    {
        return usrp_->set_clock_source(clock_source, mboard);
    }

    /** @brief Get time sources. */
    std::vector<std::string> getTimeSources(const size_t mboard = 0)
    {
        return usrp_->get_time_sources(mboard);
    }

    /** @brief Get time source. */
    std::string getTimeSource(const size_t mboard = 0)
    {
        return usrp_->get_time_source(mboard);
    }

    /** @brief Set time source. */
    void setTimeSource(const std::string &time_source,
                       const size_t mboard = uhd::usrp::multi_usrp::ALL_MBOARDS)
    {
        return usrp_->set_time_source(time_source, mboard);
    }

    /** @brief Get master clock rate. */
    double getMasterClockRate(void)
    {
        return usrp_->get_master_clock_rate(0);
    }

    /** @brief Get TX frequency. */
    double getTXFrequency(void)
    {
        return usrp_->get_tx_freq();
    }

    /** @brief Set TX frequency.
     * @param freq The center frequency
     */
    void setTXFrequency(double freq);

    /** @brief Get RX frequency. */
    double getRXFrequency(void)
    {
        return usrp_->get_rx_freq();
    }

    /** @brief Set RX frequency.
     * @param freq The center frequency
     */
    void setRXFrequency(double freq);

    /** @brief Get TX rate. */
    double getTXRate(void)
    {
        return usrp_->get_tx_rate();
    }

    /** @brief Set TX rate. */
    void setTXRate(double rate)
    {
        usrp_->set_tx_rate(rate);
        logEvent("USRP: TX rate set to %f", rate);
        tx_rate_ = usrp_->get_tx_rate();
    }

    /** @brief Get RX rate. */
    double getRXRate(void)
    {
        return usrp_->get_rx_rate();
    }

    /** @brief Set RX rate. */
    void setRXRate(double rate)
    {
        usrp_->set_rx_rate(rate);
        logEvent("USRP: RX rate set to %f", rate);
        rx_rate_ = usrp_->get_rx_rate();
    }

    /** @brief Get TX gain (dB). */
    double getTXGain(void)
    {
        return usrp_->get_tx_gain();
    }

    /** @brief Set TX gain (dB). */
    void setTXGain(float db)
    {
        return usrp_->set_tx_gain(db);
    }

    /** @brief Get RX gain (dB). */
    double getRXGain(void)
    {
        return usrp_->get_rx_gain();
    }

    /** @brief Set RX gain (dB). */
    void setRXGain(float db)
    {
        return usrp_->set_rx_gain(db);
    }

    /** @brief Get automatic DC offset correction. */
    bool getAutoDCOffset(bool enable)
    {
        return auto_dc_offset_;
    }

    /** @brief Set automatic DC offset correction. */
    void setAutoDCOffset(bool enable)
    {
        auto_dc_offset_ = enable;
        usrp_->set_rx_dc_offset(auto_dc_offset_);
        usrp_->set_tx_dc_offset(auto_dc_offset_);
    }

    /** @brief Transmit a burst of IQ buffers at the given time.
     * @param when Time at which to start the burst.
     * @param start_of_burst Is this the start of a burst?
     * @param end_of_burst Is this the end of a burst?
     * @param bufs A list of IQBuf%s to transmit.
     */
    void burstTX(std::optional<MonoClock::time_point> when,
                 bool start_of_burst,
                 bool end_of_burst,
                 std::list<std::shared_ptr<IQBuf>>& bufs);

    /** @brief Stop TX burst */
    void stopTXBurst(void);

    /** @brief Start streaming read */
    void startRXStream(MonoClock::time_point when);

    /** @brief Stop streaming read */
    void stopRXStream(void);

    /** @brief Receive specified number of samples at the given time
     * @param when The time at which to start receiving.
     * @param nsamps The number of samples to receive.
     * @param buf The IQBuf to hold received IQ samples. The buffer should be at
     * least getRecommendedBurstRXSize(nsamps) bytes.
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
        logEvent("USRP: rx_max_samps_=%lu", rx_max_samps_);
    }

    /** @brief Set the multiplier for the maximum number of samples we will read
     * at a time during burstRX.
     */
    void setMaxRXSampsFactor(unsigned n)
    {
        setMaxRXSamps(n*rx_stream_->get_max_num_samps());
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
        logEvent("USRP: tx_max_samps_=%lu", tx_max_samps_);
    }

    /** @brief Set the multiplier for the maximum number of samples we will read
     * at a time during burstTX.
     */
    void setMaxTXSampsFactor(unsigned n)
    {
        setMaxTXSamps(n*tx_stream_->get_max_num_samps());
    }

    /** @brief Return the recommended buffer size during burstRX.
     * @param nsamps Number of samples to read during burst
     * @return Recommended buffer size
     */
    size_t getRecommendedBurstRXSize(size_t nsamps)
    {
        return nsamps + 8*rx_max_samps_;
    }

    /** @brief Get the TX underflow count.
     * @return The number of TX underflow errors
     */
    /** Return the number of TX underflow errors and reset the counter */
    uint64_t getTXUnderflowCount(void)
    {
        return tx_underflow_count_.exchange(0, std::memory_order_relaxed);
    }

    /** @brief Get the TX late count.
     * @return The number of late TX packet errors
     */
    /** Return the number of TX late packet errors and reset the counter */
    uint64_t getTXLateCount(void)
    {
        return tx_late_count_.exchange(0, std::memory_order_relaxed);
    }

    /** @brief Stop processing data. */
    void stop(void);

private:
    /** @brief Our associated UHD USRP. */
    uhd::usrp::multi_usrp::sptr usrp_;

    /** @brief The DeviceType of the main device */
    DeviceType device_type_;

    /** @brief Current TX rate */
    double tx_rate_;

    /** @brief Current RX rate */
    double rx_rate_;

    /** @brief Current TX frequency */
    double tx_freq_;

    /** @brief Current RX frequency */
    double rx_freq_;

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

    /** @brief Flag indicating whether or not to enable automatic DC offset
     * correction.
     */
    bool auto_dc_offset_;

    /** @brief Flag indicating the we should stop processing data. */
    bool done_;

    /** @brief TX underflow count. */
    std::atomic<uint64_t> tx_underflow_count_;

    /** @brief TX late count. */
    std::atomic<uint64_t> tx_late_count_;

    /** @brief Thread that receives TX errors. */
    std::thread tx_error_thread_;

    /** @brief Determine the type of the main device. */
    void determineDeviceType(void);

    /** @brief Worker that receives TX errors. */
    void txErrorWorker(void);
};

#endif /* USRP_H_ */
