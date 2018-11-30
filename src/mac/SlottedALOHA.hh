#ifndef SLOTTEDALOHA_H_
#define SLOTTEDALOHA_H_

#include <random>
#include <vector>

#include "USRP.hh"
#include "phy/PHY.hh"
#include "phy/PacketDemodulator.hh"
#include "phy/PacketModulator.hh"
#include "mac/MAC.hh"
#include "mac/SlottedMAC.hh"
#include "net/Net.hh"

/** @brief A Slotted ALOHA MAC. */
class SlottedALOHA : public SlottedMAC
{
public:
    SlottedALOHA(std::shared_ptr<USRP> usrp,
                 std::shared_ptr<PHY> phy,
                 std::shared_ptr<Controller> controller,
                 std::shared_ptr<SnapshotCollector> collector,
                 const Channels &rx_channels,
                 const Channels &tx_channels,
                 std::shared_ptr<PacketModulator> modulator,
                 std::shared_ptr<PacketDemodulator> demodulator,
                 double slot_size,
                 double guard_size,
                 double demod_overlap_size,
                 double p);
    virtual ~SlottedALOHA();

    SlottedALOHA(const SlottedALOHA&) = delete;
    SlottedALOHA(SlottedALOHA&&) = delete;

    SlottedALOHA& operator=(const SlottedALOHA&) = delete;
    SlottedALOHA& operator=(SlottedALOHA&&) = delete;

    /** @brief Get probability of transmission
     */
    double getTXProb(void)
    {
        return p_;
    }

    /** @brief Set probability of transmission
     * @param p The probability of transmitting in a given slot
     */
    void setTXProb(double p)
    {
        p_ = p;
    }

    /** @brief Stop processing packets */
    void stop(void) override;

    void reconfigure(void) override;

    void sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt) override;

private:
    /** @brief Probability of transmission */
    double p_;

    /** @brief Random number generator */
    std::mt19937 gen_;

    /** @brief Uniform 0-1 real distribution */
    std::uniform_real_distribution<double> dist_;

    /** @brief Exponential distribution for inter-arrival times */
    std::exponential_distribution<double> arrival_dist_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Worker transmitting packets */
    void txWorker(void);
};

#endif /* SLOTTEDALOHA_H_ */
