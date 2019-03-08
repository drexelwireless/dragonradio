#ifndef CHANNELIZER_H_
#define CHANNELIZER_H_

#include "IQBuffer.hh"
#include "phy/Channel.hh"

/** @brief Base class for channelizers */
class Channelizer
{
public:
    Channelizer(const Channels &channels)
      : rx_rate_(0.0)
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
    }

    /** @brief Add an IQ buffer to demodulate.
     * @param buf The IQ samples to demodulate
     */
    virtual void push(const std::shared_ptr<IQBuf> &buf) = 0;

    /** @brief Reconfigure for new RX parameters */
    virtual void reconfigure(void) = 0;

protected:
    /** @brief RX sample rate */
    double rx_rate_;

    /** @brief Radio channels */
    Channels channels_;
};

#endif /* CHANNELIZER_H_ */
