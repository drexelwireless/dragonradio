// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDMA_H_
#define TDMA_H_

#include <vector>

#include "qvar.hh"
#include "Radio.hh"
#include "mac/MAC.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"

/** @brief A TDMA MAC. */
class TDMA : public MAC
{
public:
    TDMA(std::shared_ptr<Radio> radio,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<Synthesizer> synthesizer,
         double rx_period);
    virtual ~TDMA();

    TDMA(const TDMA&) = delete;
    TDMA(TDMA&&) = delete;

    TDMA& operator=(const TDMA&) = delete;
    TDMA& operator=(TDMA&&) = delete;

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
     * @param next_slotidx Slot index of next slot
     */
    void findNextSlot(WallClock::time_point t,
                      WallClock::time_point &t_next,
                      size_t &next_slotidx);

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* TDMA_H_ */
