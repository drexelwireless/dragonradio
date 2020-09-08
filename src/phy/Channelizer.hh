#ifndef CHANNELIZER_H_
#define CHANNELIZER_H_

#include "IQBuffer.hh"
#include "net/Net.hh"
#include "phy/Channel.hh"
#include "phy/PHY.hh"

/** @brief Base class for channelizers */
class Channelizer : public Element
{
public:
    Channelizer(std::shared_ptr<PHY> phy,
                double rx_rate,
                const Channels &channels)
      : source(*this, nullptr, nullptr)
      , phy_(phy)
      , rx_rate_(rx_rate)
      , channels_(channels)
    {
    }

    virtual ~Channelizer() = default;

    /** @brief Get the RX sample rate. */
    virtual double getRXRate(void)
    {
        return rx_rate_;
    }

    /** @brief Set the RX sample rate.
     * @param rate The rate.
     */
    virtual void setRXRate(double rate)
    {
        rx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get channels. */
    virtual const Channels &getChannels(void) const
    {
        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const Channels &channels)
    {
        channels_ = channels;
        reconfigure();
    }

    /** @brief Add an IQ buffer to demodulate.
     * @param buf The IQ samples to demodulate
     */
    virtual void push(const std::shared_ptr<IQBuf> &buf) = 0;

    /** @brief Reconfigure for new RX parameters */
    virtual void reconfigure(void) = 0;

    /** @brief Demodulated packets */
    RadioOut<Push> source;

protected:
    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy_;

    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief Radio channels */
    Channels channels_;
};

/** @brief Demodulate packets from a channel. */
class ChannelDemodulator {
public:
    using callback_type = PHY::PacketDemodulator::callback_type;

    ChannelDemodulator(PHY &phy,
                       const Channel &channel,
                       const std::vector<C> &taps,
                       double rx_rate)
      : channel_(channel)
      , rate_(phy.getMinRXRateOversample()*channel.bw/rx_rate)
      , fshift_(channel.fc/rx_rate)
      , demod_(phy.mkPacketDemodulator())
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
     * @param rx_rate RX rate (Hz)
     */
    virtual void timestamp(const MonoClock::time_point &timestamp,
                           std::optional<ssize_t> snapshot_off,
                           ssize_t offset,
                           float rx_rate) = 0;

    /** @brief Demodulate data with given parameters */
    virtual void demodulate(const std::complex<float>* data,
                            size_t count) = 0;

protected:
    /** @brief Channel we are demodulating */
    Channel channel_;

    /** @brief Resampling rate */
    double rate_;

    /** @brief Frequency shift */
    double fshift_;

    /** @brief Our demodulator */
    std::shared_ptr<PHY::PacketDemodulator> demod_;
};

#endif /* CHANNELIZER_H_ */
