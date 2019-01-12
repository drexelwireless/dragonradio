#ifndef PACKETDEMODULATOR_H_
#define PACKETDEMODULATOR_H_

#include "IQBuffer.hh"
#include "phy/Channel.hh"

/** @brief A packet demodulator. */
class PacketDemodulator
{
public:
    PacketDemodulator(const Channels &channels)
        : channels_(channels)
    {
    }

    virtual ~PacketDemodulator() = default;

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

    /** @brief Radio channels, given as shift from center frequency */
    Channels channels_;
};

#endif /* PACKETDEMODULATOR_H_ */
