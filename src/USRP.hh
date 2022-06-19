// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef USRP_H_
#define USRP_H_

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
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
    /** @brief A class that preserves the USRP clock across potential master
     * clock rate changes.
     */
    /**
     * When the USRP master clock rate changes, the USRP time may also change.
     * On some USRP devices, like the B210, changing the sampling rate can
     * change the master clock. This will then change the current USRP time. For
     * example, the default B210 master clock rate seems to be 32 MHz. When
     * setting the TX rate to, e.g., 1 MHZ, the master clock rate is
     * automatically changed to 16 MHz. This causes the USRP time to *double*.
     * Apparently, USRP time is stored as the number of master clock ticks, so
     * halving the master clock rate doubles the time. This RAII class attempts
     * to preserve the USRP clock across operations that potentially change the
     * master clock rate. It should come into scope before an operation that may
     * change the master clock rate. It should remain in scope for as short a
     * time as possible, but for at least the duration of the operation in
     * question.
     */
    class preserve_clock {
    public:
        preserve_clock() = delete;

        preserve_clock(USRP& usrp_)
            : usrp(usrp_)
            , clock_lock(usrp_.clock_mutex_)
            , t_last_pps(usrp.usrp_->get_time_last_pps())
            , t1_uhd(usrp.usrp_->get_time_now())
            , t1_steady(std::chrono::steady_clock::now())
        {
        }

        ~preserve_clock()
        {
            uhd::time_spec_t                      t2_uhd = usrp.usrp_->get_time_now();
            std::chrono::steady_clock::time_point t2_steady = std::chrono::steady_clock::now();
            double                                delta_pps = (t1_uhd - t_last_pps).get_real_secs();
            double                                delta_uhd = (t2_uhd - t1_uhd).get_real_secs();
            std::chrono::duration<double>         delta_steady = t2_steady - t1_steady;

            // If the difference between the USRP time before the potential
            // master clock rate change and the USRP time after the potential
            // master lock rate change is either negative or more than twice the
            // steady_clock delta, assume the master clock rate has changed and
            // reset the USRP time.
            if (delta_uhd < 0 || delta_uhd > 2*delta_steady.count()) {
                usrp.usrp_->set_time_next_pps(t_last_pps + ceil(delta_steady.count() - delta_pps));
                std::this_thread::sleep_for(1s);
            }
        }

    protected:
        /** @brief The USRP. */
        USRP& usrp;

        // Lock the clock for update
        std::lock_guard<std::mutex> clock_lock;

        /** @brief Time at last PPS. */
        uhd::time_spec_t t_last_pps;

        /** @brief UHD time before potential master lock rate change */
        uhd::time_spec_t t1_uhd;

        /** @brief Steady clock time before potential master lock rate change */
        std::chrono::steady_clock::time_point t1_steady;
    };

    USRP(const std::string& addr);
    ~USRP();

    USRP() = delete;
    USRP(const USRP&) = delete;
    USRP(USRP&&) = delete;

    USRP& operator=(const USRP&) = delete;
    USRP& operator=(USRP&&) = delete;

    /** @brief Get motherboard of this device. */
    std::string getMboard(void) const
    {
        return mboard_;
    }

    /** @brief Get automatic DC offset correction. */
    bool getAutoDCOffset() const
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

    /** @brief Get a list of possible TX antennas.
     * @param chan The channel index
     */
    std::vector<std::string> getTXAntennas(size_t chan=0) const
    {
        return usrp_->get_tx_antennas(chan);
    }

    /** @brief Set the TX antenna.
     * @param chan The channel index
     */
    std::string getTXAntenna(size_t chan=0) const
    {
        return usrp_->get_tx_antenna(chan);
    }

    /** @brief Set the TX antenna.
     * @param ant The antenna name
     * @param chan The channel index
     */
    void setTXAntenna(const std::string &ant, size_t chan=0)
    {
        usrp_->set_tx_antenna(ant, chan);
    }

    /** @brief Get a list of possible RX antennas.
     * @param chan The channel index
     */
    std::vector<std::string> getRXAntennas(size_t chan=0) const
    {
        return usrp_->get_rx_antennas(chan);
    }

    /** @brief Set the RX antenna.
     * @param chan The channel index
     */
    std::string getRXAntenna(size_t chan=0) const
    {
        return usrp_->get_rx_antenna(chan);
    }

    /** @brief Set the RX antenna.
     * @param ant The antenna name
     * @param chan The channel index
     */
    void setRXAntenna(const std::string &ant, size_t chan=0)
    {
        usrp_->set_rx_antenna(ant, chan);
    }

    /** @brief Get the TX frontend specification.
     * @param chan The channel index
     */
    std::string getTXSubdevSpec(size_t chan=0) const
    {
        return usrp_->get_tx_subdev_spec(chan).to_string();
    }

    /** @brief Set the TX frontend specification.
     * @param spec The frontend specification
     * @param chan The channel index
     */
    void setTXSubdevSpec(const std::string &spec, size_t chan=0) const
    {
        usrp_->set_tx_subdev_spec(spec, chan);
    }

    /** @brief Get the RX frontend specification.
     * @param chan The channel index
     */
    std::string getRXSubdevSpec(size_t chan=0) const
    {
        return usrp_->get_rx_subdev_spec(chan).to_string();
    }

    /** @brief Set the RX frontend specification.
     * @param spec The frontend specification
     * @param chan The channel index
     */
    void setRXSubdevSpec(const std::string &spec, size_t chan=0) const
    {
        usrp_->set_rx_subdev_spec(spec, chan);
    }

    /** @brief Get the master clock rate
     * @param mboard The motherboard
     */
    double getMasterClockRate(size_t mboard) const
    {
        return usrp_->get_master_clock_rate(mboard);
    }

    /** @brief Get the master clock rate
     * @param rate The master clock rate (Hz)
     * @param mboard The motherboard
     */
    void setMasterClockRate(double rate, size_t mboard = uhd::usrp::multi_usrp::ALL_MBOARDS)
    {
        {
            preserve_clock preserve(*this);

            usrp_->set_master_clock_rate(rate, mboard);
        }

        logUSRP(LOGDEBUG, "master clock rate set to %g", rate);
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

    /** @brief Synchronize USRP time with host.
     * @param random_bias Introduce a random bias in USRP clock (for testing).
     * @param use_pps Set time on PPS edge. Use with GPSDO.
     */
    void syncTime(bool random_bias = false, bool use_pps = false);

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
        {
            preserve_clock preserve(*this);

            usrp_->set_tx_rate(rate);
        }

        logUSRP(LOGDEBUG, "TX rate set to %g", rate);
        tx_rate_ = usrp_->get_tx_rate();
    }

    double getRXRate(void) const override
    {
        return usrp_->get_rx_rate();
    }

    void setRXRate(double rate) override
    {
        {
            preserve_clock preserve(*this);

            usrp_->set_rx_rate(rate);
        }

        logUSRP(LOGDEBUG, "RX rate set to %g", rate);
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

    /** @brief Mutex for accessing the clock. */
    mutable std::mutex clock_mutex_;

    /** @brief The motherboard */
    std::string mboard_;

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

    /** @brief Worker that receives TX errors. */
    void txErrorWorker(void);
};

#endif /* USRP_H_ */
