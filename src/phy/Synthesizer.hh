#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include <atomic>

#include "spinlock_mutex.hh"
#include "Logger.hh"
#include "net/Net.hh"
#include "phy/ModPacket.hh"
#include "phy/PHY.hh"

/** @brief Base class for synthesizers */
class Synthesizer : public Element
{
public:
    /** @brief A time slot that needs to be synthesized */
    struct Slot {
        Slot(const Clock::time_point &deadline_,
             size_t delay_,
             size_t max_samples_,
             bool overfill_)
         : deadline(deadline_)
         , delay(delay_)
         , max_samples(max_samples_)
         , overfill(overfill_)
         , closed(false)
         , nsamples(0)
        {
        }

        Slot() = delete;

        ~Slot() = default;

        /** @brief Synthesis deadline. Slot must be ready at this time! */
        const Clock::time_point deadline;

        /** @brief Number of samples to delay */
        const size_t delay;

        /** @brief Maximum number of samples in this slot */
        const size_t max_samples;

        /** @brief A flag indicating whether or not we are allowed to overfill
         * the slot and spill into the next slot.
         */
        const bool overfill;

        /** @brief When true, indicates that the slot is closed for further
         * samples.
         */
        std::atomic<bool> closed;

        /** @brief Mutex protecting slot info */
        spinlock_mutex mutex;

        /** @brief Number of samples in slot */
        size_t nsamples;

        /** @brief The list of IQ buffers */
        std::list<std::shared_ptr<IQBuf>> iqbufs;

        /** @brief The list of modulated packets */
        std::list<std::unique_ptr<ModPacket>> mpkts;

        /** @brief Push a modulated packet onto the slot
         * @param mpkt A reference to a modulated packet
         * @param overfill true if the slot can be overfilled
         * @return true if the packet was pushed, false if it didn't fit
         */
        /** The slot's mutex must be held by the caller. If pushed, the slot
         * takes ownership of the ModPacket.
         */
        bool push(std::unique_ptr<ModPacket> &mpkt, bool overfill)
        {
            if (closed.load(std::memory_order_acquire))
                return false;

            size_t n = mpkt->samples->size() - mpkt->samples->delay;

            if (nsamples + n <= max_samples || (nsamples < max_samples && overfill)) {
                mpkt->start = delay + nsamples;
                mpkt->nsamples = n;

                iqbufs.emplace_back(mpkt->samples);
                mpkts.emplace_back(std::move(mpkt));
                nsamples += n;

                return true;
            } else
                return false;
        }
    };

    Synthesizer(std::shared_ptr<PHY> phy,
                double tx_rate)
      : sink(*this, nullptr, nullptr)
      , phy_(phy)
      , tx_rate_(tx_rate)
      , max_packet_size_(0)
    {
    }

    virtual ~Synthesizer() = default;

    /** @brief Get the TX sample rate. */
    virtual double getTXRate(void)
    {
        return tx_rate_;
    }

    /** @brief Set the TX sample rate.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        tx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get maximum packet size. */
    size_t getMaxPacketSize(void)
    {
        return max_packet_size_;
    }

    /** @brief Set maximum packet size. */
    void setMaxPacketSize(size_t max_packet_size)
    {
        max_packet_size_ = max_packet_size;
    }

    /** @brief Get the maximum modulation upsample rate. */
    /** This should return the maximum upsample rate used during modulation.
     * This value is used by SmartController to estimate the maximum number of
     * packets that can fit in one time slot.
     */
    virtual double getMaxTXUpsampleRate(void) = 0;

    /** @brief Modulate a slot. */
    virtual void modulate(const std::shared_ptr<Slot> &slot) = 0;

    /** @brief Finalize a slot. */
    /** This should be called after a slot is closed in order to finish any
     * final computations necessary. It does not need to acquire teh slot's
     * mutex.
     */
    virtual void finalize(Slot &slot)
    {
    }

    /** @brief Reconfigure for new TX parameters */
    virtual void reconfigure(void) = 0;

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

protected:
    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Maximum number of possible samples in a modulated packet. */
    size_t max_packet_size_;
};

#endif /* SYNTHESIZER_H_ */
