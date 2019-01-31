#ifndef MAC_H_
#define MAC_H_

#include <memory>

#include "spinlock_mutex.hh"
#include "USRP.hh"
#include "phy/Channels.hh"
#include "phy/PHY.hh"
#include "phy/PacketDemodulator.hh"
#include "phy/PacketModulator.hh"
#include "mac/Controller.hh"
#include "mac/MAC.hh"
#include "mac/Snapshot.hh"

/** @brief A MAC protocol. */
class MAC
{
public:
    MAC(std::shared_ptr<USRP> usrp,
        std::shared_ptr<PHY> phy,
        std::shared_ptr<Controller> controller,
        std::shared_ptr<SnapshotCollector> collector,
        const Channels &rx_channels,
        const Channels &tx_channels,
        std::shared_ptr<PacketModulator> modulator,
        std::shared_ptr<PacketDemodulator> demodulator);
    virtual ~MAC() = default;

    MAC() = delete;
    MAC(const MAC&) = delete;
    MAC& operator =(const MAC&) = delete;
    MAC& operator =(MAC&&) = delete;

    /** @brief Can this MAC transmit
     * @return true if we can transmit, false otherwise
     */
    virtual bool canTransmit(void) const
    {
        return can_transmit_;
    }

    /** @brief Get the RX channels
     * @return The RX channels
     */
    virtual const Channels &getRXChannels(void) const
    {
        return rx_channels_;
    }

    /** @brief Set the RX channels
     * @param channels The RX channels
     */
    virtual void setRXChannels(const Channels &channels)
    {
        rx_channels_ = channels;
    }

    /** @brief Get the TX channels
     * @return The TX channels
     */
    virtual const Channels &getTXChannels(void) const
    {
        return tx_channels_;
    }

    /** @brief Set the TX channels
     * @param channels The TX channels
     */
    virtual void setTXChannels(const Channels &channels)
    {
        tx_channels_ = channels;
        // Ensure that the current TX channel still exists
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
        if (channel >= tx_channels_.size()) {
            logEvent("MAC: illegal channel: channel=%lu, nchannels=%lu",
                channel,
                tx_channels_.size());
            channel = 0;
        }

        tx_channel_ = channel;

        modulator_->setTXChannel(channel);

        logEvent("MAC: Set TX channel: channel=%lu; freq offset = %f",
            channel,
            getTXShift());
    }

    /** @brief Get the frequency shift to use during transmission
     * @return The frequency shift (Hz) from center frequency
     */
    virtual double getTXShift(void) const
    {
        return tx_channels_.size() > 0 ? tx_channels_[tx_channel_] : 0.0;
    }

    /** @brief Reconfigure the MAC when after parameters change */
    virtual void reconfigure(void);

    /** @brief Stop processing packets. */
    virtual void stop(void) = 0;

    /** @brief Send a timestamped packet after the given time.
     * @param t The time after which the packet should be sent
     * @param pkt The packet.
     */
    /** The MAC layer is responsible for adding a timestamp control message to
     * the packet. The packet is expected to be transmitted at the time
     * contained in the timestamp, which will be no sooner than t.
     */
    virtual void sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt) = 0;

    /** @brief Actually timestamp a packet with the given timestamp and add it
     * to the timestamp slot.
     */
    virtual void timestampPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt);

protected:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> usrp_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Our controller */
    std::shared_ptr<Controller> controller_;

    /** @brief Our snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector_;

    /** @brief Flag indicating whether or not we can transmit */
    /** This is used, in particular, for the TDMA MAC, which may not have a slot
     */
    bool can_transmit_;

    /** @brief RX channels */
    Channels rx_channels_;

    /** @brief TX channels */
    Channels tx_channels_;

    /** @brief Our packet modulator. */
    std::shared_ptr<PacketModulator> modulator_;

    /** @brief Our packet demodulator. */
    std::shared_ptr<PacketDemodulator> demodulator_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;

    /** @brief Transmission channel */
    Channels::size_type tx_channel_;

    /** @brief Mutex for timestamped packet */
    spinlock_mutex timestamped_mutex_;

    /** @brief TX time for timestamped packet. Should be the beginning of a
     * slot.
     */
    Clock::time_point timestamped_deadline_;

    /** @brief Modulated timestamped packet */
    std::unique_ptr<ModPacket> timestamped_mpkt_;
};

#endif /* MAC_H_ */
