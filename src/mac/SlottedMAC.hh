// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

#include <atomic>
#include <mutex>
#include <optional>
#include <queue>

#include "qvar.hh"
#include "Radio.hh"
#include "mac/MAC.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"

/** @brief A slotted MAC. */
class SlottedMAC : public MAC
{
public:
    SlottedMAC(std::shared_ptr<Radio> radio,
               std::shared_ptr<Controller> controller,
               std::shared_ptr<SnapshotCollector> collector,
               std::shared_ptr<Channelizer> channelizer,
               std::shared_ptr<Synthesizer> synthesizer,
               double rx_period,
               unsigned nsyncthreads);
    virtual ~SlottedMAC();

    SlottedMAC(const SlottedMAC&) = delete;
    SlottedMAC(SlottedMAC&&) = delete;

    SlottedMAC& operator=(const SlottedMAC&) = delete;
    SlottedMAC& operator=(SlottedMAC&&) = delete;

    virtual std::chrono::duration<double> getSlotSendLeadTime(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        return slot_send_lead_time_;
    }

    virtual void setSlotSendLeadTime(std::chrono::duration<double> t)
    {
        modify([&](){
            slot_send_lead_time_ = t;

            reconfigure();
        });
    }

    void stop(void) override;

protected:
    /** @brief Lead time needed to send a slot's worth of data (sec) */
    std::chrono::duration<double> slot_send_lead_time_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Do we need to stop the current burst? */
    std::atomic<bool> stop_burst_;

    /** @brief Slot to transmit */
    qvar<std::optional<TXSlot>> tx_slot_;

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
    virtual void findNextSlot(WallClock::time_point t,
                              WallClock::time_point& t_next,
                              size_t& next_slotidx) = 0;

    /** @brief Decide whether or not to transmit in a slot
     * @param t Time at which slot starts
     * @param slotidx Slot index
     * @returns True if a slot was found, false otherwise
     */
    virtual bool transmitInSlot(WallClock::time_point t,
                                size_t slotidx);

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* SLOTTEDMAC_H_ */
