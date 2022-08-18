// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SLOTTEDALOHA_H_
#define SLOTTEDALOHA_H_

#include <random>
#include <vector>

#include "Radio.hh"
#include "mac/SlottedMAC.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"

/** @brief A Slotted ALOHA MAC. */
class SlottedALOHA : public SlottedMAC
{
public:
    SlottedALOHA(std::shared_ptr<Radio> radio,
                 std::shared_ptr<Controller> controller,
                 std::shared_ptr<SnapshotCollector> collector,
                 std::shared_ptr<Channelizer> channelizer,
                 std::shared_ptr<Synthesizer> synthesizer,
                 double rx_period,
                 double p);
    virtual ~SlottedALOHA();

    SlottedALOHA(const SlottedALOHA&) = delete;
    SlottedALOHA(SlottedALOHA&&) = delete;

    SlottedALOHA& operator=(const SlottedALOHA&) = delete;
    SlottedALOHA& operator=(SlottedALOHA&&) = delete;

    /** @brief Get slot index to transmit on */
    size_t getSlotIndex(void) const
    {
        return slotidx_.load(std::memory_order_relaxed);
    }

    /** @brief Set slot to transmit on */
    void setSlotIndex(size_t slotidx)
    {
        slotidx_.store(slotidx, std::memory_order_relaxed);
    }

    /** @brief Get probability of transmission */
    double getTXProb(void) const
    {
        return p_.load(std::memory_order_relaxed);
    }

    /** @brief Set probability of transmission
     * @param p The probability of transmitting in a given slot
     */
    void setTXProb(double p)
    {
        p_.store(p, std::memory_order_relaxed);
    }

private:
    /** @brief Slot index to use */
    std::atomic<size_t> slotidx_;

    /** @brief Probability of transmission */
    std::atomic<double> p_;

    /** @brief Random number generator */
    std::mt19937 gen_;

    /** @brief Uniform 0-1 real distribution */
    std::uniform_real_distribution<double> dist_;

    void findNextSlot(WallClock::time_point t,
                      WallClock::time_point& t_next,
                      size_t& next_slotidx) override;

    bool transmitInSlot(WallClock::time_point t,
                        size_t slotidx) override;

    void reconfigure(void) override;
};

#endif /* SLOTTEDALOHA_H_ */
