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
    using slots_type = std::vector<bool>;

    TDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator,
         double bandwidth,
         size_t nslots,
         double slot_size,
         double guard_size);
    virtual ~TDMA();

    TDMA(const TDMA&) = delete;
    TDMA(TDMA&&) = delete;

    TDMA& operator=(const TDMA&) = delete;
    TDMA& operator=(TDMA&&) = delete;

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

    /** @brief Get number of slots
     */
    slots_type::size_type size(void);

    /** @brief Set number of slots
     * @param n The number of slots
     */
    void resize(slots_type::size_type n);

    /** @brief Access a slot */
    slots_type::reference operator [](size_t i);

    /** @brief Return an iterator to the beginning of slots. */
    slots_type::iterator begin(void);

    /** @brief Return an iterator to the end of slots. */
    slots_type::iterator end(void);

    /** @brief Stop processing packets */
    void stop(void) override;

private:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> usrp_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Our packet modulator. */
    std::shared_ptr<PacketModulator> modulator_;

    /** @brief Our packet demodulator. */
    std::shared_ptr<PacketDemodulator> demodulator_;

    /** @brief Bandwidth */
    double bandwidth_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;

    /** @brief Length of TDMA frame (sec) */
    double frame_size_;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief Length of inter-slot guard (sec) */
    double guard_size_;

    /** @brief The slot schedule */
    slots_type slots_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(Clock::time_point t, Clock::time_point &t_next);

    /** @brief Transmit one slot's worth of samples */
    void txSlot(Clock::time_point when, size_t maxSamples);

    /** @brief Reconfigure the MAC when TDMA parameters change */
    void reconfigure(void);
};

#endif /* TDMA_H_ */
