#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include <atomic>

#include "spinlock_mutex.hh"
#include "Logger.hh"
#include "mac/Schedule.hh"
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
             size_t deadline_delay_,
             size_t max_samples_,
             size_t max_superslot_samples_,
             size_t slotidx_)
         : deadline(deadline_)
         , deadline_delay(deadline_delay_)
         , max_superslot_samples(max_superslot_samples_)
         , slotidx(slotidx_)
         , closed(false)
         , max_samples(max_samples_)
         , delay(0)
         , nsamples(0)
        {
        }

        Slot() = delete;

        ~Slot() = default;

        /** @brief Synthesis deadline. Slot must be ready at this time! */
        const Clock::time_point deadline;

        /** @brief Number of samples to delay the deadline */
        const size_t deadline_delay;

        /** @brief Maximum number of samples in this slot if it is a superslot */
        const size_t max_superslot_samples;

        /** @brief The schedule slot this slot represents */
        const size_t slotidx;

        /** @brief When true, indicates that the slot is closed for further
         * samples.
         */
        std::atomic<bool> closed;

        /** @brief Mutex protecting slot info */
        spinlock_mutex mutex;

        /** @brief Maximum number of samples in this slot */
        size_t max_samples;

        /** @brief Number of samples to delay */
        size_t delay;

        /** @brief Number of samples in slot */
        size_t nsamples;

        /** @brief The list of IQ buffers */
        std::list<std::shared_ptr<IQBuf>> iqbufs;

        /** @brief The list of modulated packets */
        std::list<std::unique_ptr<ModPacket>> mpkts;

        /** @brief The length of the slot, in samples. */
        /** Return the length of the slot, in samples. This does not include
         * delayed samples.
         */
        size_t length(void)
        {
            return nsamples - delay;
        }

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

            if (nsamples + n <= delay + max_samples || (nsamples < delay + max_samples && overfill)) {
                mpkt->start = deadline_delay + nsamples;
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
                double tx_rate,
                const Channels &channels)
      : sink(*this, nullptr, nullptr)
      , phy_(phy)
      , tx_rate_(tx_rate)
      , superslots_(false)
      , channels_(channels)
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

    /** @brief Get superslots flag */
    bool getSuperslots(void) const
    {
        return superslots_.load(std::memory_order_relaxed);
    }

    /** @brief Set superslots flag */
    void setSuperslots(bool superslots)
    {
        superslots_.store(superslots, std::memory_order_relaxed);
    }

    /** @brief Get channels. */
    virtual const Channels &getChannels(void) const
    {
        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const Channels &channels)
    {
        channels_ = channels;
        reconfigure();
    }

    /** @brief Get schedule. */
    virtual const Schedule &getSchedule(void) const
    {
        return schedule_;
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule &schedule)
    {
        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        schedule_ = schedule;
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
    double getMaxTXUpsampleRate(void)
    {
        double max_rate = 1.0;

        for (auto it = channels_.begin(); it != channels_.end(); ++it) {
            double rate = tx_rate_/(phy_->getMinTXRateOversample()*it->first.bw);

            if (rate > max_rate)
                max_rate = rate;
        }

        return max_rate;
    }

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

    /** @brief Use superslots */
    std::atomic<bool> superslots_;

    /** @brief Radio channels */
    Channels channels_;

    /** @brief Radio schedule */
    Schedule schedule_;

    /** @brief Maximum number of possible samples in a modulated packet. */
    size_t max_packet_size_;
};

#endif /* SYNTHESIZER_H_ */
