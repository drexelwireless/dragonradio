#ifndef SLOTTEDMAC_H_
#define SLOTTEDMAC_H_

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
               std::shared_ptr<SnapshotCollector> collector,
               const Channels &rx_channels,
               const Channels &tx_channels,
               std::shared_ptr<PacketModulator> modulator,
               std::shared_ptr<PacketDemodulator> demodulator,
               double slot_size,
               double guard_size,
               double demod_overlap_size);
    virtual ~SlottedMAC();

    SlottedMAC(const SlottedMAC&) = delete;
    SlottedMAC(SlottedMAC&&) = delete;

    SlottedMAC& operator=(const SlottedMAC&) = delete;
    SlottedMAC& operator=(SlottedMAC&&) = delete;

    /** @brief Get slot size, including guard interval */
    virtual double getSlotSize(void);

    /** @brief Set slot size, including guard interval
     * @param t Slot size in seconds
     */
    virtual void setSlotSize(double t);

    /** @brief Get guard interval size */
    virtual double getGuardSize(void);

    /** @brief Set guard interval size
     * @param t Guard interval size in seconds
     */
    virtual void setGuardSize(double t);

    /** @brief Get demodulation overlap size */
    virtual double getDemodOverlapSize(void);

    /** @brief Set demodulation overlap size
     * @param t Overlap size in seconds
     */
    virtual void setDemodOverlapSize(double t);

    /** @brief Get number of slots to pre-modulate */
    virtual double getPreModulateSlots(void);

    /** @brief Set demodulation overlap size
     * @param n Number of slots to pre-modulate
     */
    virtual void setPreModulateSlots(double n);

    virtual void reconfigure(void) override;

protected:
    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    double guard_size_;

    /** @brief Size of demodulation overlap (sec) */
    double demod_overlap_size_;

    /** @brief Number of RX samples in a full slot */
    size_t rx_slot_samps_;

    /** @brief Number of TX samples in the non-guard portion of a slot */
    size_t tx_slot_samps_;

    /** @brief Number of TX samples in the entire slot, including the guard */
    size_t tx_full_slot_samps_;

    /** @brief Number of slots to pre-modulate */
    double premod_slots_;

    /** @brief Number of samples to pre-modulate */
    size_t premod_samps_;

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
