// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SLOTSYNTHESIZER_H_
#define SLOTSYNTHESIZER_H_

#include "phy/Synthesizer.hh"

/** @brief Base class for synthesizers */
class SlotSynthesizer : public Synthesizer
{
public:
    /** @brief A time slot that needs to be synthesized */
    struct Slot {
        Slot(const WallClock::time_point &deadline_,
             size_t deadline_delay_,
             size_t max_samples_,
             size_t full_slot_samples_,
             size_t slotidx_,
             size_t nchannels)
         : deadline(deadline_)
         , deadline_delay(deadline_delay_)
         , full_slot_samples(full_slot_samples_)
         , slotidx(slotidx_)
         , closed(false)
         , max_samples(max_samples_)
         , delay(0)
         , nsamples(0)
         , npartial(0)
        {
            nfinished.store(0, std::memory_order_relaxed);
        }

        Slot() = delete;

        ~Slot() = default;

        /** @brief Synthesis deadline. Slot must be ready at this time! */
        const WallClock::time_point deadline;

        /** @brief Number of samples to delay the deadline */
        const size_t deadline_delay;

        /** @brief Number of samples in a full slot including any guard */
        const size_t full_slot_samples;

        /** @brief The schedule slot this slot represents */
        const size_t slotidx;

        /** @brief When true, indicates that the slot is closed for further
         * samples.
         */
        std::atomic<bool> closed;

        /** @brief Mutex protecting slot info */
        std::mutex mutex;

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

        /** @brief Number of threads who have finished with this slot */
        std::atomic<unsigned> nfinished;

        /** @brief Frequency-domain IQ buffer */
        std::unique_ptr<IQBuf> fdbuf;

        /** @brief Number of valid samples in the frequency-domain buffer */
        size_t fdnsamples;

        /** @brief Number of samples represented by final FFT block that were
         * part of the slot.
         */
        size_t npartial;

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
        bool push(std::unique_ptr<ModPacket> &mpkt,
                  bool overfill)
        {
            if (closed.load(std::memory_order_acquire))
                return false;

            size_t n = mpkt->nsamples;

            if (nsamples + n <= delay + max_samples || (nsamples < delay + max_samples && overfill)) {
                mpkt->start = deadline_delay + nsamples;

                iqbufs.emplace_back(mpkt->samples);
                mpkts.emplace_back(std::move(mpkt));
                nsamples += n;

                return true;
            } else
                return false;
        }
    };

    SlotSynthesizer(const std::vector<PHYChannel> &channels,
                    double tx_rate,
                    unsigned nsyncthreads)
      : Synthesizer(channels, tx_rate, nsyncthreads)
    {
    }

    virtual ~SlotSynthesizer() = default;

    std::optional<size_t> getHighWaterMark(void) const override
    {
        return std::nullopt;
    }

    void setHighWaterMark(std::optional<size_t> high_water_mark) override
    {
    }

    bool isEnabled(void) const override
    {
        return true;
    }

    void enable(void) override
    {
    }

    void disable(void) override
    {
    }

    TXRecord try_pop(void) override
    {
        return TXRecord{};
    }

    TXRecord pop(void) override
    {
        return TXRecord{};
    }

    TXRecord pop_for(const std::chrono::duration<double>& rel_time) override
    {
        return TXRecord{};
    }

    void push_slot(const WallClock::time_point& when, size_t slot, ssize_t prev_oversample) override
    {
    }

    TXSlot pop_slot(void) override
    {
        return TXSlot{};
    }

    /** @brief Modulate a slot. */
    virtual void modulate(const std::shared_ptr<Slot> &slot) = 0;

    /** @brief Finalize a slot. */
    /** This should be called after a slot is closed in order to finish any
     * final computations necessary. It does not need to acquire the slot's
     * mutex.
     */
    virtual void finalize(Slot &slot)
    {
    }
};

#endif /* SLOTSYNTHESIZER_H_ */
