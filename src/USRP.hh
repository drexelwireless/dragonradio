// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef USRP_H_
#define USRP_H_

#include <atomic>
#include <deque>
#include <string>
#include <thread>

#include <uhd/version.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include "logging.hh"
#include "Clock.hh"
#include "IQBuffer.hh"
#include "Radio.hh"

/** @brief A USRP. */
class USRP : public Radio
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
    DeviceType getDeviceType(void) const
    {
        return device_type_;
    }

    /** @brief Get clock sources. */
    std::vector<std::string> getClockSources(const size_t mboard = 0) const
    {
        return usrp_->get_clock_sources(mboard);
    }

    /** @brief Get clock source. */
    std::string getClockSource(const size_t mboard = 0) const
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
    std::vector<std::string> getTimeSources(const size_t mboard = 0) const
    {
        return usrp_->get_time_sources(mboard);
    }

    /** @brief Get time source. */
    std::string getTimeSource(const size_t mboard = 0) const
    {
        return usrp_->get_time_source(mboard);
    }

    /** @brief Set time source. */
    void setTimeSource(const std::string &time_source,
                       const size_t mboard = uhd::usrp::multi_usrp::ALL_MBOARDS)
    {
        return usrp_->set_time_source(time_source, mboard);
    }

    /** @brief Synchronize USRP time with host. */
    void syncTime(bool random_bias = false);

    /** @brief Get automatic DC offset correction. */
    bool getAutoDCOffset(bool enable) const
    {
        return auto_dc_offset_;
    }

    /** @brief Set automatic DC offset correction. */
    void setAutoDCOffset(bool enable)
    {
        auto_dc_offset_ = enable;
        usrp_->set_rx_dc_offset(auto_dc_offset_);
        usrp_->set_tx_dc_offset(auto_dc_offset_);
    };

    /** @brief Return the maximum number of samples we will read at a time
     * during burstRX.
     */
    size_t getMaxRXSamps(void) const
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
    void setMaxRXSampsFactor(unsigned n)
    {
        setMaxRXSamps(n*rx_stream_->get_max_num_samps());
    }

    /** @brief Return the maximum number of samples we will write at a time
     * during burstTX.
     */
    size_t getMaxTXSamps(void) const
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
    void setMaxTXSampsFactor(unsigned n)
    {
        setMaxTXSamps(n*tx_stream_->get_max_num_samps());
    }

    double getMasterClockRate(void) const override
    {
        return usrp_->get_master_clock_rate(0);
    }

    double getTXFrequency(void) const override
    {
        return usrp_->get_tx_freq();
    }

    void setTXFrequency(double freq) override;

    double getRXFrequency(void) const override
    {
        return usrp_->get_rx_freq();
    }

    void setRXFrequency(double freq) override;

    double getTXRate(void) const override
    {
        return usrp_->get_tx_rate();
    }

    void setTXRate(double rate) override
    {
        usrp_->set_tx_rate(rate);
        logUSRP(LOGDEBUG, "TX rate set to %f", rate);
        tx_rate_ = usrp_->get_tx_rate();
    }

    double getRXRate(void) const override
    {
        return usrp_->get_rx_rate();
    }

    void setRXRate(double rate) override
    {
        usrp_->set_rx_rate(rate);
        logUSRP(LOGDEBUG, "RX rate set to %f", rate);
        rx_rate_ = usrp_->get_rx_rate();
    }

    double getTXGain(void) const override
    {
        return usrp_->get_tx_gain();
    }
    void setTXGain(float db) override
    {
        return usrp_->set_tx_gain(db);
    }

    double getRXGain(void) const override
    {
        return usrp_->get_rx_gain();
    }

    void setRXGain(float db) override
    {
        return usrp_->set_rx_gain(db);
    }

    std::optional<MonoClock::time_point> getNextTXTime() const override
    {
        return t_next_tx_;
    }

    void burstTX(std::optional<MonoClock::time_point> when,
                 bool start_of_burst,
                 bool end_of_burst,
                 std::list<std::shared_ptr<IQBuf>>& bufs) override;

    void stopTXBurst(void) override;

    void startRXStream(MonoClock::time_point when) override;

    void stopRXStream(void) override;

    bool burstRX(MonoClock::time_point when, size_t nsamps, IQBuf& buf) override;

    size_t getRecommendedBurstRXSize(size_t nsamps) const override
    {
        return nsamps + 8*rx_max_samps_;
    }

    uint64_t getTXUnderflowCount(void) override
    {
        return tx_underflow_count_.exchange(0, std::memory_order_relaxed);
    }

    uint64_t getTXLateCount(void) override
    {
        return tx_late_count_.exchange(0, std::memory_order_relaxed);
    }

    void stop(void) override;

    MonoClock::time_point now() noexcept override;

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

    /** @brief Time at which next transmission will occur */
    std::optional<MonoClock::time_point> t_next_tx_;

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
