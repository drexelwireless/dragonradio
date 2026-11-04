// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef USRP_H_
#define USRP_H_

#include <atomic>
#include <map>
#include <deque>
#include <list>
#include <optional>
#include <stdexcept>
#include <vector>
#include <string>
#include <thread>

#include <uhd/usrp/multi_usrp.hpp>

#include "logging.hh"
#include "Clock.hh"
#include "IQBuffer.hh"

/** @brief A radio device: the IQ and clock boundary used by the MAC.
 *
 * Two implementations exist. UHDUSRP drives real Ettus hardware through UHD
 * and is what every hardware run uses. SimUSRP (SimUSRP.hh) exchanges
 * timestamped IQ bursts with other simulated nodes. The MAC, channelizer and
 * synthesizer only see this interface.
 */
class USRP
{
public:
    enum DeviceType {
        kUSRPN210,
        kUSRPX310,
        kUSRPUnknown,
        kSimulated
    };

    static constexpr size_t kAllMboards = static_cast<size_t>(~0);

    USRP()
      : tx_max_samps_(0)
      , rx_max_samps_(0)
      , tx_underflow_count_(0)
      , tx_late_count_(0)
    {
    }

    virtual ~USRP() = default;

    USRP(const USRP&) = delete;
    USRP(USRP&&) = delete;

    USRP& operator=(const USRP&) = delete;
    USRP& operator=(USRP&&) = delete;

    /** @brief Get type of this device. */
    virtual DeviceType getDeviceType(void) = 0;

    /** @brief Get clock sources. */
    virtual std::vector<std::string> getClockSources(const size_t mboard = 0) = 0;

    /** @brief Get clock source. */
    virtual std::string getClockSource(const size_t mboard = 0) = 0;

    /** @brief Set clock source. */
    virtual void setClockSource(const std::string clock_source,
                                const size_t mboard = kAllMboards) = 0;

    /** @brief Get time sources. */
    virtual std::vector<std::string> getTimeSources(const size_t mboard = 0) = 0;

    /** @brief Get time source. */
    virtual std::string getTimeSource(const size_t mboard = 0) = 0;

    /** @brief Set time source. */
    virtual void setTimeSource(const std::string &time_source,
                               const size_t mboard = kAllMboards) = 0;

    /** @brief Get master clock rate. */
    virtual double getMasterClockRate(void) = 0;

    /** @brief Get TX frequency. */
    virtual double getTXFrequency(void) = 0;

    /** @brief Set TX frequency. */
    virtual void setTXFrequency(double freq) = 0;

    /** @brief Get RX frequency. */
    virtual double getRXFrequency(void) = 0;

    /** @brief Set RX frequency. */
    virtual void setRXFrequency(double freq) = 0;

    /** @brief Get TX rate. */
    virtual double getTXRate(void) = 0;

    /** @brief Set TX rate. */
    virtual void setTXRate(double rate) = 0;

    /** @brief Get RX rate. */
    virtual double getRXRate(void) = 0;

    /** @brief Set RX rate. */
    virtual void setRXRate(double rate) = 0;

    /** @brief Get TX gain (dB). */
    virtual double getTXGain(void) = 0;

    /** @brief Set TX gain (dB). */
    virtual void setTXGain(float db) = 0;

    /** @brief Get RX gain (dB). */
    virtual double getRXGain(void) = 0;

    /** @brief Set RX gain (dB). */
    virtual void setRXGain(float db) = 0;

    /** @brief Align device time to a shared PPS (time source "external"). */
    /** Waits for a PPS edge, then sets the time of the following edge to the
     * host's whole second, so every radio on the same PPS whose host clock
     * agrees (NTP) to well under half a second gets the same device time.
     * Returns the host time of the observed edge minus its whole second, in
     * [-0.5, 0.5): near +/-0.5 the radios may disagree by one second.
     * Throws if no edge arrives within timeout seconds. Not supported by
     * the simulated device.
     */
    virtual double syncTimeToPPS(double timeout)
    {
        throw std::runtime_error("syncTimeToPPS: not supported by this device");
    }

    /** @brief Get automatic DC offset correction. */
    bool getAutoDCOffset(bool enable)
    {
        return auto_dc_offset_;
    }

    /** @brief Set automatic DC offset correction. */
    virtual void setAutoDCOffset(bool enable) = 0;

    /** @brief Get time at which next transmission will occur */
    std::optional<MonoClock::time_point> getNextTXTime()
    {
        return t_next_tx_;
    }

    /** @brief Transmit a burst of IQ buffers at the given time.
     * @param when Time at which to start the burst.
     * @param start_of_burst Is this the start of a burst?
     * @param end_of_burst Is this the end of a burst?
     * @param bufs A list of IQBuf%s to transmit.
     */
    virtual void burstTX(std::optional<MonoClock::time_point> when,
                         bool start_of_burst,
                         bool end_of_burst,
                         std::list<std::shared_ptr<IQBuf>>& bufs) = 0;

    /** @brief Stop TX burst */
    virtual void stopTXBurst(void) = 0;

    /** @brief Start streaming read */
    virtual void startRXStream(MonoClock::time_point when) = 0;

    /** @brief Stop streaming read */
    virtual void stopRXStream(void) = 0;

    /** @brief Receive specified number of samples at the given time
     * @param when The time at which to start receiving.
     * @param nsamps The number of samples to receive.
     * @param buf The IQBuf to hold received IQ samples. The buffer should be at
     * least getRecommendedBurstRXSize(nsamps) bytes.
     * @returns Returns true if the burst was successfully received, false
     * otherwise.
     */
    virtual bool burstRX(MonoClock::time_point when, size_t nsamps, IQBuf& buf) = 0;

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
        logUSRP(LOGDEBUG, "rx_max_samps_=%lu", rx_max_samps_);
    }

    /** @brief Set the multiplier for the maximum number of samples we will read
     * at a time during burstRX.
     */
    virtual void setMaxRXSampsFactor(unsigned n) = 0;

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
        logUSRP(LOGDEBUG, "tx_max_samps_=%lu", tx_max_samps_);
    }

    /** @brief Set the multiplier for the maximum number of samples we will read
     * at a time during burstTX.
     */
    virtual void setMaxTXSampsFactor(unsigned n) = 0;

    /** @brief Return the recommended buffer size during burstRX.
     * @param nsamps Number of samples to read during burst
     * @return Recommended buffer size
     */
    size_t getRecommendedBurstRXSize(size_t nsamps)
    {
        return nsamps + 8*rx_max_samps_;
    }

    /** Return the number of TX underflow errors and reset the counter */
    uint64_t getTXUnderflowCount(void)
    {
        return tx_underflow_count_.exchange(0, std::memory_order_relaxed);
    }

    /** Return the number of TX late packet errors and reset the counter */
    uint64_t getTXLateCount(void)
    {
        return tx_late_count_.exchange(0, std::memory_order_relaxed);
    }

    /** @brief Stop processing data. */
    virtual void stop(void) = 0;

protected:
    /** @brief Maximum number of samples we will send at a time during burstTX.
     */
    size_t tx_max_samps_;

    /** @brief Maximum number of samples we will read at a time during burstRX.
     */
    size_t rx_max_samps_;

    /** @brief Time at which next transmission will occur */
    std::optional<MonoClock::time_point> t_next_tx_;

    /** @brief Flag indicating whether or not to enable automatic DC offset
     * correction.
     */
    bool auto_dc_offset_ = false;

    /** @brief TX underflow count. */
    std::atomic<uint64_t> tx_underflow_count_;

    /** @brief TX late count. */
    std::atomic<uint64_t> tx_late_count_;
};

/** @brief A USRP driven through UHD (the hardware implementation). */
class UHDUSRP : public USRP
{
public:
    UHDUSRP(const std::string& addr,
            const std::optional<std::string>& tx_subdev,
            const std::optional<std::string>& rx_subdev,
            double freq,
            const std::string& tx_ant,
            const std::string& rx_ant,
            float tx_gain,
            float rx_gain);
    virtual ~UHDUSRP();

    DeviceType getDeviceType(void) override
    {
        return device_type_;
    }

    std::vector<std::string> getClockSources(const size_t mboard = 0) override
    {
        return usrp_->get_clock_sources(mboard);
    }

    std::string getClockSource(const size_t mboard = 0) override
    {
        return usrp_->get_clock_source(mboard);
    }

    void setClockSource(const std::string clock_source,
                        const size_t mboard = kAllMboards) override
    {
        return usrp_->set_clock_source(clock_source, mboard);
    }

    std::vector<std::string> getTimeSources(const size_t mboard = 0) override
    {
        return usrp_->get_time_sources(mboard);
    }

    std::string getTimeSource(const size_t mboard = 0) override
    {
        return usrp_->get_time_source(mboard);
    }

    void setTimeSource(const std::string &time_source,
                       const size_t mboard = kAllMboards) override
    {
        return usrp_->set_time_source(time_source, mboard);
    }

    double getMasterClockRate(void) override
    {
        return usrp_->get_master_clock_rate(0);
    }

    double syncTimeToPPS(double timeout) override;

    double getTXFrequency(void) override
    {
        return usrp_->get_tx_freq();
    }

    void setTXFrequency(double freq) override;

    double getRXFrequency(void) override
    {
        return usrp_->get_rx_freq();
    }

    void setRXFrequency(double freq) override;

    double getTXRate(void) override
    {
        return usrp_->get_tx_rate();
    }

    void setTXRate(double rate) override
    {
        usrp_->set_tx_rate(rate);
        logUSRP(LOGDEBUG, "TX rate set to %f", rate);
        tx_rate_ = usrp_->get_tx_rate();
    }

    double getRXRate(void) override
    {
        return usrp_->get_rx_rate();
    }

    void setRXRate(double rate) override
    {
        usrp_->set_rx_rate(rate);
        logUSRP(LOGDEBUG, "RX rate set to %f", rate);
        rx_rate_ = usrp_->get_rx_rate();
    }

    double getTXGain(void) override
    {
        return usrp_->get_tx_gain();
    }

    void setTXGain(float db) override
    {
        return usrp_->set_tx_gain(db);
    }

    double getRXGain(void) override
    {
        return usrp_->get_rx_gain();
    }

    void setRXGain(float db) override
    {
        return usrp_->set_rx_gain(db);
    }

    void setAutoDCOffset(bool enable) override
    {
        auto_dc_offset_ = enable;
        usrp_->set_rx_dc_offset(auto_dc_offset_);
        usrp_->set_tx_dc_offset(auto_dc_offset_);
    }

    void burstTX(std::optional<MonoClock::time_point> when,
                 bool start_of_burst,
                 bool end_of_burst,
                 std::list<std::shared_ptr<IQBuf>>& bufs) override;

    void stopTXBurst(void) override;

    void startRXStream(MonoClock::time_point when) override;

    void stopRXStream(void) override;

    bool burstRX(MonoClock::time_point when, size_t nsamps, IQBuf& buf) override;

    void setMaxRXSampsFactor(unsigned n) override
    {
        setMaxRXSamps(n*rx_stream_->get_max_num_samps());
    }

    void setMaxTXSampsFactor(unsigned n) override
    {
        setMaxTXSamps(n*tx_stream_->get_max_num_samps());
    }

    void stop(void) override;

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

    /** @brief Flag indicating the we should stop processing data. */
    bool done_;

    /** @brief Thread that receives TX errors. */
    std::thread tx_error_thread_;

    /** @brief Determine the type of the main device. */
    void determineDeviceType(void);

    /** @brief Worker that receives TX errors. */
    void txErrorWorker(void);
};

/** Read-only check of a UHD device's external reference: opens the device
 * (no tuning, no streams, nothing transmitted), selects the given clock and
 * time sources, reports whether the reference is locked ("yes", "no", or
 * "unknown" without a ref_locked sensor) and how many PPS edges arrive in
 * the given time, then restores the previous sources.
 */
std::map<std::string, std::string> checkReferenceUHD(const std::string& addr,
                                                     const std::string& clock_source,
                                                     const std::string& time_source,
                                                     double seconds);

#endif /* USRP_H_ */
