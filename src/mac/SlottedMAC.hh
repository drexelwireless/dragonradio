#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

#include <optional>
#include <queue>

#include "spinlock_mutex.hh"
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
               double slot_modulate_lead_time,
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

    virtual size_t getSlotModulateLeadTime(void)
    {
        return slot_modulate_lead_time_;
    }

    virtual void setSlotModulateLeadTime(size_t t)
    {
        slot_modulate_lead_time_ = t;
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

protected:
    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    double guard_size_;

    /** @brief Lead time needed to modulate a slot's worth of data. */
    double slot_modulate_lead_time_;

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

    /** @brief Is the next slot the start of a burst? */
    bool next_slot_start_of_burst_;

    /** @brief Mutex protecting synthesized slots */
    spinlock_mutex slots_mutex_;

    /** @brief Synthesized slots */
    std::queue<std::shared_ptr<Synthesizer::Slot>> slots_;

    /** @brief TX center frequency offset from RX center frequency. */
    /** If the TX and RX rates are different, this is non-empty and contains
     * the frequency of the channel we transmit on.
     */
    std::optional<double> tx_fc_off_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Schedule modulation of a slot
     * @param when Start time of slot
     * @param prev_overfill Number of overfill samples from previous slot.
     * @param slotidx Index of the slot to modulated
     */
    void modulateSlot(Clock::time_point when,
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
    std::shared_ptr<Synthesizer::Slot> finalizeSlot(Clock::time_point when);

    /** @brief Transmit a slot
     * @param slot The slot
     */
    void txSlot(std::shared_ptr<Synthesizer::Slot> &&slot);

    /** @brief Mark a slot as missed
     * @param slot The slot
     */
    void missedSlot(Synthesizer::Slot &slot);
};

#endif /* SLOTTEDMAC_H_ */
