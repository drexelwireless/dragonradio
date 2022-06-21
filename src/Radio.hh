// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef RADIO_HH_
#define RADIO_HH_

#include <chrono>
#include <list>

// #include "logging.hh"
#include "Clock.hh"
#include "IQBuffer.hh"

/** @brief A Radio. */
class Radio : public MonoClock::TimeKeeper
{
public:
    Radio() = default;
    virtual ~Radio() = default;

    /** @brief Get master clock rate. */
    virtual double getMasterClockRate(void) const = 0;

    /** @brief Get TX frequency. */
    virtual double getTXFrequency(void) const = 0;

    /** @brief Set TX frequency.
     * @param freq The center frequency
     */
    virtual void setTXFrequency(double freq) = 0;

    /** @brief Get RX frequency. */
    virtual double getRXFrequency(void) const = 0;

    /** @brief Set RX frequency.
     * @param freq The center frequency
     */
    virtual void setRXFrequency(double freq) = 0;

    /** @brief Get TX rate. */
    virtual double getTXRate(void) const = 0;

    /** @brief Set TX rate. */
    virtual void setTXRate(double rate) = 0;

    /** @brief Get RX rate. */
    virtual double getRXRate(void) const = 0;

    /** @brief Set RX rate. */
    virtual void setRXRate(double rate) = 0;

    /** @brief Get TX gain (dB). */
    virtual double getTXGain(void) const = 0;

    /** @brief Set TX gain (dB). */
    virtual void setTXGain(float db) = 0;

    /** @brief Get RX gain (dB). */
    virtual double getRXGain(void) const = 0;

    /** @brief Set RX gain (dB). */
    virtual void setRXGain(float db) = 0;

    /** @brief Get TX lead time (sec) */
    virtual std::chrono::duration<double> getTXLeadTime(void) const
    {
        return tx_lead_time_.load(std::memory_order_acquire);
    }

    /** @brief Set TX lead time (sec) */
    virtual void setTXLeadTime(std::chrono::duration<double> t)
    {
        tx_lead_time_.store(t, std::memory_order_release);
    }

    /** @brief Return true if radio is currently transmitting a burst */
    virtual bool inTXBurst() const = 0;

    /** @brief Get monotonic time at which next transmission will occur */
    /** This is the time at which the next transmission would occur. The next TX
     * time can be non-nullopt even if we are not in a burst.
     */
    virtual std::optional<MonoClock::time_point> getNextTXTime() const = 0;

    /** @brief Transmit zero samples
     * @param n Number of zero samples to transmit
     */
    virtual void zeroStuff(ssize_t n) = 0;

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
    virtual void startRXStream(std::optional<MonoClock::time_point> when = std::nullopt) = 0;

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
    virtual bool burstRX(std::optional<MonoClock::time_point> when, size_t nsamps, IQBuf& buf) = 0;

    /** @brief Return the recommended buffer size during burstRX.
     * @param nsamps Number of samples to read during burst
     * @return Recommended buffer size
     */
    virtual size_t getRecommendedBurstRXSize(size_t nsamps) const = 0;

    /** @brief Get the TX underflow count.
     * @return The number of TX underflow errors
     */
    /** Return the number of TX underflow errors and reset the counter */
    virtual uint64_t getTXUnderflowCount(void) = 0;

    /** @brief Get the TX late count.
     * @return The number of late TX packet errors
     */
    /** Return the number of TX late packet errors and reset the counter */
    virtual uint64_t getTXLateCount(void) = 0;

    /** @brief Stop processing data. */
    virtual void stop(void) = 0;

    /** @brief Get the current monotonic time.
     * @return A
     */
    MonoClock::time_point now() noexcept override
    {
        return MonoClock::time_point{std::chrono::steady_clock::now().time_since_epoch()};
    }

 protected:
    /** @brief Lead time needed for transmission (sec) */
    std::atomic<std::chrono::duration<double>> tx_lead_time_;
};

#endif /* RADIO_HH_ */
