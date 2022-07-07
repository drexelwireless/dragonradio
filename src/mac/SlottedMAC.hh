// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

#include <atomic>
#include <mutex>
#include <optional>
#include <queue>

#include "qvar.hh"
#include "Clock.hh"
#include "Logger.hh"
#include "SafeQueue.hh"
#include "Radio.hh"
#include "phy/Channelizer.hh"
#include "phy/SlotSynthesizer.hh"
#include "mac/MAC.hh"
#include "mac/Schedule.hh"

/** @brief A slotted MAC. */
class SlottedMAC : public MAC
{
public:
    using Slot = SlotSynthesizer::Slot;

    SlottedMAC(std::shared_ptr<Radio> radio,
               std::shared_ptr<Controller> controller,
               std::shared_ptr<SnapshotCollector> collector,
               std::shared_ptr<Channelizer> channelizer,
               std::shared_ptr<SlotSynthesizer> synthesizer,
               double slot_size,
               double guard_size,
               double slot_send_lead_time,
               unsigned nsyncthreads);
    virtual ~SlottedMAC();

    SlottedMAC(const SlottedMAC&) = delete;
    SlottedMAC(SlottedMAC&&) = delete;

    SlottedMAC& operator=(const SlottedMAC&) = delete;
    SlottedMAC& operator=(SlottedMAC&&) = delete;

    /** @brief Get slot size, including guard interval */
    virtual std::chrono::duration<double> getSlotSize(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        return slot_size_;
    }

    /** @brief Set slot size, including guard interval
     * @param t Slot size in seconds
     */
    virtual void setSlotSize(std::chrono::duration<double> t)
    {
        modify([&](){
            rx_period_ = t;
            slot_size_ = t;

            reconfigure();
        });
    }

    /** @brief Get guard interval size */
    virtual std::chrono::duration<double> getGuardSize(void)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        return guard_size_;
    }

    /** @brief Set guard interval size
     * @param t Guard interval size in seconds
     */
    virtual void setGuardSize(std::chrono::duration<double> t)
    {
        modify([&](){
            guard_size_ = t;

            reconfigure();
        });
    }

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

    /** @brief Is this MAC FDMA? */
    virtual bool isFDMA(void) const
    {
        return false;
    }

    void stop(void) override;

protected:
    /** @brief Our slot synthesizer. */
    std::shared_ptr<SlotSynthesizer> slot_synthesizer_;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    std::chrono::duration<double> slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    std::chrono::duration<double> guard_size_;

    /** @brief Lead time needed to send a slot's worth of data (sec) */
    std::chrono::duration<double> slot_send_lead_time_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Do we need to stop the current burst? */
    std::atomic<bool> stop_burst_;

    /** @brief The next slot */
    std::shared_ptr<Slot> next_slot_;

    /** @brief Slot to transmit */
    qvar<std::shared_ptr<Slot>> tx_slot_;

    /** @brief Worker transmitting slots */
    void txWorker(void);

    /** @brief Schedule modulation of a slot
     * @param when Start time of slot
     * @param prev_overfill Number of overfill samples from previous slot.
     * @param slotidx Index of the slot to modulated
     */
    void modulateSlot(WallClock::time_point when,
                      size_t prev_overfill,
                      size_t slotidx);

    /** @brief Finalize the next TX slot.
     * @param when Start time of slot
     * @return The slot
     */
    /** After finalizeSlot returns, the caller has exclusive access to the slot.
     * That is, it does not need to acquire the slot's lock to modify it,
     * because it is guaranteed exclusive access.
     */
    std::shared_ptr<Slot> finalizeSlot(WallClock::time_point when);

    /** @brief Transmit a slot
     * @param slot The slot
     */
    void txSlot(std::shared_ptr<Slot> &&slot)
    {
        tx_slot_ = std::move(slot);
    }

    /** @brief Mark a slot as missed
     * @param slot The slot
     */
    void missedSlot(Slot &slot);

    void wake_dependents() override;

    void reconfigure(void) override;
};

#endif /* SLOTTEDMAC_H_ */
