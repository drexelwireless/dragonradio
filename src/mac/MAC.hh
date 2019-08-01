#ifndef MAC_H_
#define MAC_H_

#include <memory>

#include "spinlock_mutex.hh"
#include "USRP.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
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
        std::shared_ptr<Channelizer> channelizer,
        std::shared_ptr<Synthesizer> synthesizer);
    virtual ~MAC() = default;

    MAC() = delete;
    MAC(const MAC&) = delete;
    MAC& operator =(const MAC&) = delete;
    MAC& operator =(MAC&&) = delete;

    /** @brief Get the MAC's channelizer */
    Channelizer &getChannelizer(void)
    {
        return *channelizer_;
    }

    /** @brief Get the MAC's synthesizer */
    Synthesizer &getSynthesizer(void)
    {
        return *synthesizer_;
    }

    /** @brief Can this MAC transmit
     * @return true if we can transmit, false otherwise
     */
    virtual bool canTransmit(void) const
    {
        return can_transmit_;
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

    /** @brief Our channelizer. */
    std::shared_ptr<Channelizer> channelizer_;

    /** @brief Our synthesizer. */
    std::shared_ptr<Synthesizer> synthesizer_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;

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
