#ifndef PACKETMODULATOR_H_
#define PACKETMODULATOR_H_

#include "Logger.hh"
#include "phy/Channels.hh"
#include "phy/ModPacket.hh"

/** @brief A packet modulator. */
class PacketModulator
{
public:
    PacketModulator(const Channels &channels)
        : tx_rate_(0.0)
        , chan_rate_(0.0)
        , channels_(channels)
        , tx_channel_(0)
        , maxPacketSize_(0)
    {
    }

    virtual ~PacketModulator() = default;

    /** @brief Get the TX sample rate. */
    virtual double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Set the TX sample rate.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        tx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get the channel sample rate. */
    virtual double getChannelRate(void)
    {
        return chan_rate_;
    }

    /** @brief Set the channel sample rate.
     * @param rate The rate.
     */
    virtual void setChannelRate(double rate)
    {
        chan_rate_ = rate;
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
        setTXChannel(tx_channel_);
    }

    /** @brief Get the frequency channel to use during transmission
     * @return The frequency channel
     */
    virtual Channels::size_type getTXChannel(void) const
    {
        return tx_channel_;
    }

    /** @brief Set the frequency channel to use during transmission
     * @param The frequency channel
     */
    virtual void setTXChannel(Channels::size_type channel)
    {
        if (channel >= channels_.size()) {
            logEvent("PHY: illegal channel: channel=%lu, nchannels=%lu",
                channel,
                channels_.size());
            channel = 0;
        }

        tx_channel_ = channel;
    }

    /** @brief Get the frequency shift to use during transmission
     * @return The frequency shift (Hz) from center frequency
     */
    virtual double getTXShift(void) const
    {
        return channels_.size() > 0 ? channels_[tx_channel_] : 0.0;
    }

    /** @brief Get maximum packet size. */
    size_t getMaxPacketSize(void)
    {
        return maxPacketSize_;
    }

    /** @brief Set maximum packet size. */
    void setMaxPacketSize(size_t maxPacketSize)
    {
        maxPacketSize_ = maxPacketSize;
    }

    /** @brief Modulate samples.
     * @param n The number of samples to produce.
     */
    virtual void modulate(size_t n) = 0;

    /** @brief Pop a list of modulated packet such that the total number of
     * modulated samples is maxSamples or fewer.
     * @param pkts A reference to a list to which the popped packets will be
     * appended.
     * @param maxSample The maximum number of samples to pop.
     * @param overfill Completely fill the slot, even if it means overfilling it
     * @return The number of samples popped
     */
    virtual size_t pop(std::list<std::unique_ptr<ModPacket>>& pkts,
                       size_t maxSamples,
                       bool overfill) = 0;

    /** @brief Reconfigure for new TX parameters */
    virtual void reconfigure(void) = 0;

protected:
    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Per-channel sample rate */
    double chan_rate_;

    /** @brief Radio channels, given as shift from center frequency */
    Channels channels_;

    /** @brief Transmission channel, given shift from center frequency */
    Channels::size_type tx_channel_;

    /** @brief Maximum number of possible samples in a modulated packet. */
    size_t maxPacketSize_;
};

#endif /* PACKETMODULATOR_H_ */
