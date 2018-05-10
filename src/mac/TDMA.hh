#ifndef TDMA_H_
#define TDMA_H_

#include <liquid/liquid.h>

#include <vector>
#include <complex>

#include "PacketDemodulator.hh"
#include "PacketModulator.hh"
#include "USRP.hh"
#include "phy/PHY.hh"
#include "mac/MAC.hh"
#include "net/Net.hh"

/** @brief A TDMA MAC. */
class TDMA : public MAC
{
public:
    TDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator,
         double bandwidth,
         size_t nslots,
         double slot_size,
         double guard_size);
    virtual ~TDMA();

    /** @brief Get number of slots
     */
    size_t getNumSlots(void);

    /** @brief Set number of slots
     * @param n The number of slots
     */
    void setNumSlots(size_t n);

    /** @brief Get slot size, including guard interval
     */
    double getSlotSize(void);

    /** @brief Set slot size, including guard interval
     * @param t Slot size in seconds
     */
    void setSlotSize(double t);

    /** @brief Get guard interval size
     */
    double getGuardSize(void);

    /** @brief Set guard interval size
     * @param t Guard interval size in seconds
     */
    void setGuardSize(double t);

    /** @brief Mark a slot as belonging to us */
    void addSlot(size_t i);

    /** @brief Mark a slot as not belonging to us */
    void removeSlot(size_t i);

    /** @brief Stop processing packets */
    void stop(void) override;

private:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> _usrp;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> _phy;

    /** @brief Our packet modulator. */
    std::shared_ptr<PacketModulator> _modulator;

    /** @brief Our packet demodulator. */
    std::shared_ptr<PacketDemodulator> _demodulator;

    /** @brief Bandwidth */
    double _bandwidth;

    /** @brief RX rate */
    double _rx_rate;

    /** @brief TX rate */
    double _tx_rate;

    /** @brief Length of TDMA frame (sec) */
    double _frame_size;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double _slot_size;

    /** @brief Length of inter-slot guard (sec) */
    double _guard_size;

    /** @brief The slot schedule */
    std::vector<bool> _slots;

    /** @brief Flag indicating if we should stop processing packets */
    bool _done;

    /** @brief Thread running rxWorker */
    std::thread _rxThread;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Thread running txWorker */
    std::thread _txThread;

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @returns The beginning of the next TX slot.
     */
    Clock::time_point findNextSlot(Clock::time_point t);

    /** @brief Transmit one slot's worth of samples */
    void txSlot(Clock::time_point when, size_t maxSamples);

    /** @brief Reconfigure the MAC when TDMA parameters change */
    void reconfigure(void);
};

#endif /* TDMA_H_ */
