#ifndef TDMA_H_
#define TDMA_H_

#include <vector>

#include "USRP.hh"
#include "phy/PHY.hh"
#include "phy/PacketDemodulator.hh"
#include "phy/PacketModulator.hh"
#include "mac/MAC.hh"
#include "mac/SlottedMAC.hh"
#include "net/Net.hh"

/** @brief A TDMA MAC. */
class TDMA : public SlottedMAC
{
public:
    using slots_type = std::vector<bool>;

    TDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator,
         double bandwidth,
         double slot_size,
         double guard_size,
         size_t nslots);
    virtual ~TDMA();

    TDMA(const TDMA&) = delete;
    TDMA(TDMA&&) = delete;

    TDMA& operator=(const TDMA&) = delete;
    TDMA& operator=(TDMA&&) = delete;

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

    void sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt) override;

private:
    /** @brief Length of TDMA frame (sec) */
    double frame_size_;

    /** @brief The slot schedule */
    slots_type slots_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(Clock::time_point t, Clock::time_point &t_next);

    /** @brief Reconfigure the MAC when TDMA parameters change */
    void reconfigure(void) override;
};

#endif /* TDMA_H_ */
