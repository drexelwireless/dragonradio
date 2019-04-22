#ifndef SLOTTEDALOHA_H_
#define SLOTTEDALOHA_H_

#include <random>
#include <vector>

#include "USRP.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
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
                 std::shared_ptr<Channelizer> channelizer,
                 std::shared_ptr<Synthesizer> synthesizer,
                 double slot_size,
                 double guard_size,
                 double slot_modulate_lead_time,
                 double slot_send_lead_time,
                 double p);
    virtual ~SlottedALOHA();

    SlottedALOHA(const SlottedALOHA&) = delete;
    SlottedALOHA(SlottedALOHA&&) = delete;

    SlottedALOHA& operator=(const SlottedALOHA&) = delete;
    SlottedALOHA& operator=(SlottedALOHA&&) = delete;

    /** @brief Get slot index to transmit on */
    size_t getSlotIndex(void) const
    {
        return slotidx_;
    }

    /** @brief Set slot to transmit on */
    void setSlotIndex(size_t slotidx)
    {
        slotidx_ = slotidx;
    }

    /** @brief Get probability of transmission */
    double getTXProb(void) const
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

private:
    /** @brief Slot index to use */
    size_t slotidx_;

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
