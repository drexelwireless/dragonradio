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
    class Slots
    {
    public:
        using slots_type = std::vector<bool>;

        explicit Slots(TDMA &mac, size_t nslots)
          : mac_(mac)
          , slots_(nslots, false)
        {
            mac.reconfigure();
        }

        Slots() = delete;
        Slots(const Slots&) = delete;
        Slots(Slots&&) = delete;

        Slots& operator=(const Slots&) = delete;
        Slots& operator=(Slots&&) = delete;

        Slots& operator=(const slots_type& slots)
        {
            slots_ = slots;
            mac_.reconfigure();

            return *this;
        }

        /** @brief Get number of slots
         */
        slots_type::size_type size(void)
        {
            return slots_.size();
        }

        /** @brief Set number of slots
         * @param n The number of slots
         */
        void resize(slots_type::size_type n)
        {
            slots_.resize(n, false);
            mac_.reconfigure();
        }

        /** @brief Access a slot */
        slots_type::reference operator [](size_t i)
        {
            return slots_.at(i);
        }

        /** @brief Return an iterator to the beginning of slots. */
        slots_type::iterator begin(void)
        {
            return slots_.begin();
        }

        /** @brief Return an iterator to the end of slots. */
        slots_type::iterator end(void)
        {
            return slots_.end();
        }

    private:
        /** @brief The TDMA MAC for this slot schedule */
        TDMA &mac_;

        /** @brief The slot schedule */
        slots_type slots_;
    };

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

    /** @brief TDMA slots */
    Slots &getSlots(void)
    {
        return slots_;
    }

    /** @brief Get superslots flag */
    bool getSuperslots(void) const
    {
        return superslots_;
    }

    /** @brief Set superslots flag */
    void setSuperslots(bool superslots)
    {
        superslots_ = superslots;
    }

    void reconfigure(void) override;

private:
    /** @brief Length of TDMA frame (sec) */
    double frame_size_;

    /** @brief The slot schedule */
    Slots slots_;

    /** @brief Use superslots */
    bool superslots_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Find next TX slot
     * @param t Time at which to start looking for a TX slot
     * @param t_next The beginning of the next TX slot
     * @param owns_next_slot Will contain true if the next slow is also owned
     * @returns True if a slot was found, false otherwise
     */
    bool findNextSlot(Clock::time_point t,
                      Clock::time_point &t_next,
                      bool &owns_next_slot);
};

#endif /* TDMA_H_ */
