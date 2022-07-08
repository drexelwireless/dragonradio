// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDMA_H_
#define TDMA_H_

#include <vector>

#include "Radio.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"
#include "mac/MAC.hh"
#include "mac/SlottedMAC.hh"

/** @brief A TDMA MAC. */
class TDMA : public SlottedMAC
{
public:
    using TDMASchedule = std::vector<bool>;

    TDMA(std::shared_ptr<Radio> radio,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<SlotSynthesizer> synthesizer,
         double slot_size,
         double guard_size,
         double slot_send_lead_time,
         size_t nslots);
    virtual ~TDMA();

    TDMA(const TDMA&) = delete;
    TDMA(TDMA&&) = delete;

    TDMA& operator=(const TDMA&) = delete;
    TDMA& operator=(TDMA&&) = delete;

    /** @brief Stop processing packets */
    void stop(void) override;

    /** @brief Get number of slots */
    size_t getNSlots(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        return nslots_;
    }

    bool isFDMA(void) const override
    {
        return schedule_.isFDMA();
    }

private:
    /** @brief Length of TDMA frame (sec) */
    std::chrono::duration<double> frame_size_;

    /** @brief Number of TDMA slots */
    size_t nslots_;

    /** @brief The TDMA schedule */
    TDMASchedule tdma_schedule_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Thread running txSlotWorker */
    std::thread tx_slot_thread_;

    /** @brief Thread running txNotifier */
    std::thread tx_notifier_thread_;

    /** @brief Worker preparing slots for transmission */
    void txSlotWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @param next_slotidx Slot index of next slot
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(WallClock::time_point t,
                      WallClock::time_point &t_next,
                      size_t &next_slotidx);

    void reconfigure(void) override;
};

#endif /* TDMA_H_ */
