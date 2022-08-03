// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SLOTTEDALOHA_H_
#define SLOTTEDALOHA_H_

#include <random>
#include <vector>

#include "qvar.hh"
#include "Radio.hh"
#include "mac/MAC.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"

/** @brief A Slotted ALOHA MAC. */
class SlottedALOHA : public MAC
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

    /** @brief Stop processing packets */
    void stop(void) override;

private:
    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Do we need to stop the current burst? */
    std::atomic<bool> stop_burst_;

    /** @brief Slot to transmit */
    qvar<std::optional<TXSlot>> tx_slot_;

    /** @brief Slot index to use */
    std::atomic<size_t> slotidx_;

    /** @brief Probability of transmission */
    std::atomic<double> p_;

    /** @brief Random number generator */
    std::mt19937 gen_;

    /** @brief Uniform 0-1 real distribution */
    std::uniform_real_distribution<double> dist_;

    /** @brief Exponential distribution for inter-arrival times */
    std::exponential_distribution<double> arrival_dist_;

    /** @brief Mutex for waking threads. */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking threads. */
    std::condition_variable wake_cond_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Thread running txSlotWorker */
    std::thread tx_slot_thread_;

    /** @brief Thread running txNotifier */
    std::thread tx_notifier_thread_;

    /** @brief Worker transmitting slots */
    void txWorker(void);

    /** @brief Worker preparing slots for transmission */
    void txSlotWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(WallClock::time_point t,
                      WallClock::time_point& t_next);

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* SLOTTEDALOHA_H_ */
