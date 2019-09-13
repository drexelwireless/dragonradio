#ifndef TDMA_H_
#define TDMA_H_

#include <vector>

#include "USRP.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "mac/MAC.hh"
#include "mac/SlottedMAC.hh"
#include "net/Net.hh"

/** @brief A TDMA MAC. */
class TDMA : public SlottedMAC
{
public:
    using TDMASchedule = std::vector<bool>;

    TDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<Synthesizer> synthesizer,
         double slot_size,
         double guard_size,
         double slot_modulate_lead_time,
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
        return nslots_;
    }

    void reconfigure(void) override;

private:
    /** @brief Length of TDMA frame (sec) */
    double frame_size_;

    /** @brief Number of TDMA slots */
    size_t nslots_;

    /** @brief The TDMA schedule */
    TDMASchedule tdma_schedule_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Thread running txNotifier */
    std::thread tx_notifier_thread_;

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @param slotidx Slot index
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(Clock::time_point t,
                      Clock::time_point &t_next,
                      size_t &slotidx);
};

#endif /* TDMA_H_ */
