// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CHANNELIZER_H_
#define CHANNELIZER_H_

#include "sync_barrier.hh"
#include "IQBuffer.hh"
#include "Neighborhood.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"

/** @brief Base class for channelizers */
class Channelizer : public Element, protected sync_barrier
{
public:
    Channelizer(const std::vector<PHYChannel> &channels,
                double rx_rate,
                unsigned nsyncthreads)
      : sync_barrier(nsyncthreads)
      , source(*this, nullptr, nullptr)
      , channels_(channels)
      , rx_rate_(rx_rate)
    {
    }

    virtual ~Channelizer() = default;

    /** @brief Get channels. */
    virtual std::vector<PHYChannel> getChannels(void) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const std::vector<PHYChannel> &channels)
    {
        modify([&]() { channels_ = channels; reconfigure(); });
    }

    /** @brief Get the RX sample rate. */
    virtual double getRXRate(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return rx_rate_;
    }

    /** @brief Set the RX sample rate.
     * @param rate The rate.
     */
    virtual void setRXRate(double rate)
    {
        modify([&]() { rx_rate_ = rate; reconfigure(); }, [&](){ return rx_rate_ != rate; });
    }

    /** @brief Add an IQ buffer to demodulate.
     * @param buf The IQ samples to demodulate
     */
    virtual void push(const std::shared_ptr<IQBuf> &buf) = 0;

    /** @brief Demodulated packets */
    RadioOut<Push> source;

protected:
    /** @brief Radio channels */
    std::vector<PHYChannel> channels_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief Reconfigure for new RX parameters */
    virtual void reconfigure(void) = 0;
};

/** @brief Demodulate packets from a channel. */
class ChannelDemodulator {
public:
    using callback_type = PHY::PacketDemodulator::callback_type;

    ChannelDemodulator(unsigned chanidx,
                       const PHYChannel &channel,
                       double rx_rate)
      : chanidx_(chanidx)
      , channel_(channel)
      , rx_rate_(rx_rate)
      , rate_(channel.channel.bw/rx_rate)
      , fshift_(channel.channel.fc/rx_rate)
      , demod_(channel.phy->mkPacketDemodulator(chanidx, channel.channel))
    {
    }

    ChannelDemodulator() = delete;

    virtual ~ChannelDemodulator() = default;

    /** @brief Set demodulation callback */
    void setCallback(callback_type callback)
    {
        demod_->setCallback(callback);
    }

    /** @brief Reset internal state */
    virtual void reset(void) = 0;

    /** @brief Set timestamp for demodulation
     * @param timestamp The timestamp for future samples.
     * @param snapshot_off The snapshot offset associated with the given
     * timestamp.
     * @param offset The offset of the first sample that will be demodulated.
     * Can be negative!
     */
    virtual void timestamp(const MonoClock::time_point &timestamp,
                           std::optional<ssize_t> snapshot_off,
                           ssize_t offset) = 0;

    /** @brief Demodulate data with given parameters */
    virtual void demodulate(const std::complex<float>* data,
                            size_t count) = 0;

protected:
    /** @brief Index of channel we are demodulating */
    unsigned chanidx_;

    /** @brief Channel we are demodulating */
    PHYChannel channel_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief Resampling rate */
    double rate_;

    /** @brief Frequency shift */
    double fshift_;

    /** @brief Our demodulator */
    std::shared_ptr<PHY::PacketDemodulator> demod_;
};

#endif /* CHANNELIZER_H_ */
