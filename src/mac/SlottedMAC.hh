#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

#include <atomic>
#include <optional>
#include <queue>

#include "ringbuffer.hh"
#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "Logger.hh"
#include "USRP.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "mac/MAC.hh"
#include "mac/Schedule.hh"
#include "net/Net.hh"

/** @brief A slotted MAC. */
class SlottedMAC : public MAC
{
public:
    SlottedMAC(std::shared_ptr<USRP> usrp,
               std::shared_ptr<PHY> phy,
               std::shared_ptr<Controller> controller,
               std::shared_ptr<SnapshotCollector> collector,
               std::shared_ptr<Channelizer> channelizer,
               std::shared_ptr<Synthesizer> synthesizer,
               double slot_size,
               double guard_size,
               double slot_send_lead_time);
    virtual ~SlottedMAC();

    SlottedMAC(const SlottedMAC&) = delete;
    SlottedMAC(SlottedMAC&&) = delete;

    SlottedMAC& operator=(const SlottedMAC&) = delete;
    SlottedMAC& operator=(SlottedMAC&&) = delete;

    /** @brief Get slot size, including guard interval */
    virtual double getSlotSize(void)
    {
        return slot_size_;
    }

    /** @brief Set slot size, including guard interval
     * @param t Slot size in seconds
     */
    virtual void setSlotSize(double t)
    {
        slot_size_ = t;
        reconfigure();
    }

    /** @brief Get guard interval size */
    virtual double getGuardSize(void)
    {
        return guard_size_;
    }

    /** @brief Set guard interval size
     * @param t Guard interval size in seconds
     */
    virtual void setGuardSize(double t)
    {
        guard_size_ = t;
        reconfigure();
    }

    virtual size_t getSlotSendLeadTime(void)
    {
        return slot_send_lead_time_;
    }

    virtual void setSlotSendLeadTime(size_t t)
    {
        slot_send_lead_time_ = t;
        reconfigure();
    }

    /** @brief Get MAC schedule */
    virtual const Schedule &getSchedule(void) const
    {
        return schedule_;
    }

    /** @brief Set MAC schedule */
    virtual void setSchedule(const Schedule &schedule)
    {
        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set MAC schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        schedule_ = schedule;
        reconfigure();
    }

    virtual void reconfigure(void) override;

    /** @brief Is this MAC FDMA? */
    virtual bool isFDMA(void) const
    {
        return false;
    }

protected:
    using slot_queue = std::queue<std::shared_ptr<Synthesizer::Slot>>;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    double guard_size_;

    /** @brief Lead time needed to send a slot's worth of data. */
    double slot_send_lead_time_;

    /** @brief The MAC schedule */
    Schedule schedule_;

    /** @brief Number of RX samples in a full slot */
    size_t rx_slot_samps_;

    /** @brief RX buffer size */
    size_t rx_bufsize_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Do we need to stop the current burst? */
    std::atomic<bool> stop_burst_;

    /** @brief TX center frequency offset from RX center frequency. */
    /** If the TX and RX rates are different, this is non-empty and contains
     * the frequency of the channel we transmit on.
     */
    std::optional<double> tx_fc_off_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Mutex for transmitted slots */
    std::mutex txed_slots_mutex_;

    /** @brief Condition variable protecting transmitted slots */
    std::condition_variable txed_slots_cond_;

    /** @brief Queue of transmitted slots */
    std::queue<std::shared_ptr<Synthesizer::Slot>> txed_slots_q_;

    /** @brief Slots to transmit */
    ringbuffer<std::shared_ptr<Synthesizer::Slot>, 4> tx_slots_;

    /** @brief Worker transmitting slots */
    void txWorker(void);

    /** @brief Worker handling notification for transmitted slots */
    void txNotifier(void);

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Schedule modulation of a slot
     * @param q The slot queue
     * @param when Start time of slot
     * @param prev_overfill Number of overfill samples from previous slot.
     * @param slotidx Index of the slot to modulated
     */
    void modulateSlot(slot_queue &q,
                      Clock::time_point when,
                      size_t prev_overfill,
                      size_t slotidx);

    /** @brief Finalize the next TX slot.
     * @param q The slot queue
     * @param when Start time of slot
     * @return The slot
     */
    /** After finalizeSlot returns, the caller has exclusive access to the slot.
     * That is, it does not need to acquire the slot's lock to modify it,
     * because it is guaranteed exclusive access.
     */
    std::shared_ptr<Synthesizer::Slot> finalizeSlot(slot_queue &q,
                                                    Clock::time_point when);

    /** @brief Transmit a slot
     * @param slot The slot
     */
    void txSlot(std::shared_ptr<Synthesizer::Slot> &&slot)
    {
        tx_slots_.push(std::move(slot));
    }

    /** @brief Mark a slot as missed
     * @param slot The slot
     */
    void missedSlot(Synthesizer::Slot &slot);

    /** @brief Mark all remaining slots in qeueue as missed
     * @param q The slot queue
     */
    void missedRemainingSlots(slot_queue &q);
};

#endif /* SLOTTEDMAC_H_ */
