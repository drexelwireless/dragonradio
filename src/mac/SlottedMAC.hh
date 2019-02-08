#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

#include <optional>

#include "Logger.hh"
#include "USRP.hh"
#include "phy/PHY.hh"
#include "phy/PacketDemodulator.hh"
#include "phy/PacketModulator.hh"
#include "mac/MAC.hh"
#include "net/Net.hh"

/** @brief A slotted MAC. */
class SlottedMAC : public MAC
{
public:
    SlottedMAC(std::shared_ptr<USRP> usrp,
               std::shared_ptr<PHY> phy,
               std::shared_ptr<Controller> controller,
               std::shared_ptr<SnapshotCollector> collector,
               std::shared_ptr<PacketModulator> modulator,
               std::shared_ptr<PacketDemodulator> demodulator,
               double slot_size,
               double guard_size);
    virtual ~SlottedMAC() = default;

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

    /** @brief Get number of slots to pre-modulate */
    virtual double getPreModulateSlots(void)
    {
        return premod_slots_;
    }

    /** @brief Set demodulation overlap size
     * @param n Number of slots to pre-modulate
     */
    virtual void setPreModulateSlots(double n)
    {
        premod_slots_ = n;
        reconfigure();
    }

    /** @brief Do we have a pending timestamp packet?
     * @param when Time to check for pending timestamp
     * @return true if there is a pending timstamp at given time, false
     * otherwise.
     */
    bool pendingTimestamp(Clock::time_point when)
    {
        return timestamped_mpkt_ && approx(timestamped_deadline_, when);
    }

    virtual void reconfigure(void) override;

protected:
    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    double guard_size_;

    /** @brief Number of RX samples in a full slot */
    size_t rx_slot_samps_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief TX center frequency offset from RX center frequency. */
    /** If the TX and RX rates are different, this is non-empty and contains
     * the frequency of the channel we transmit on.
     */
    std::optional<double> tx_fc_off_;

    /** @brief Number of slots to pre-modulate */
    double premod_slots_;

    /** @brief Number of samples to pre-modulate */
    size_t premod_samps_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Transmit one slot's worth of samples
     * @param when Start time of slot
     * @param maxSamples The maximum number of samples to transmits
     * @param overfill Flag that is true if we are allowed to overfill the slot
     * @return The number of samples overfilled
     */
    virtual size_t txSlot(Clock::time_point when, size_t maxSamples, bool overfill);
};

#endif /* SLOTTEDMAC_H_ */
