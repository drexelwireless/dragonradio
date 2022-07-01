// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <chrono>
#include <optional>
#include <vector>

/** @brief A schedule specifying the channels on which a node may transmit in a
 * given slot.
 */
class Schedule {
public:
    using slot_type = std::vector<bool>;
    using sched_type = std::vector<slot_type>;

    Schedule()
      : slot_size_(std::chrono::duration<double>(0.0))
      , guard_size_(std::chrono::duration<double>(0.0))
      , frame_size_(std::chrono::duration<double>(0.0))
      , superslots_(false)
    {
    }

    Schedule(const sched_type &schedule,
             std::chrono::duration<double> slot_size = std::chrono::duration<double>(0.0),
             std::chrono::duration<double> guard_size = std::chrono::duration<double>(0.0),
             bool superslots = false)
      : schedule_(schedule)
      , slot_size_(slot_size)
      , guard_size_(guard_size)
      , superslots_(superslots)
    {
        validate();
    }

    Schedule(sched_type &&schedule,
             std::chrono::duration<double> slot_size = std::chrono::duration<double>(0.0),
             std::chrono::duration<double> guard_size = std::chrono::duration<double>(0.0),
             bool superslots = false)
      : schedule_(schedule)
      , slot_size_(slot_size)
      , guard_size_(guard_size)
      , superslots_(superslots)
    {
        validate();
    }

    Schedule(const Schedule&) = default;
    Schedule(Schedule&&) = default;

    Schedule& operator=(const Schedule&) = default;
    Schedule& operator=(Schedule&&) = default;

    Schedule& operator=(const sched_type &schedule)
    {
        schedule_ = schedule;

        validate();

        return *this;
    }

    Schedule& operator=(sched_type &&schedule)
    {
        schedule_ = std::move(schedule);

        validate();

        return *this;
    }

    /** @brief Get slot size */
    std::chrono::duration<double> getSlotSize() const
    {
        return slot_size_;
    }

    /** @brief Set slot size */
    void setSlotSize(std::chrono::duration<double> slot_size)
    {
        slot_size_ = slot_size;
        frame_size_ = nslots()*slot_size_;
    }

    /** @brief Get guard size */
    std::chrono::duration<double> getGuardSize() const
    {
        return guard_size_;
    }

    /** @brief Set guard size */
    void setGuardSize(std::chrono::duration<double> guard_size)
    {
        guard_size_ = guard_size;
    }

    /** @brief Get frame size */
    std::chrono::duration<double> getFrameSize() const
    {
        return frame_size_;
    }

    /** @brief Get superslots */
    bool getSuperslots() const
    {
        return superslots_;
    }

    /** @brief Set superslots */
    void setSuperslots(bool superslots)
    {
        superslots_ = superslots;
    }

    /** @brief Return number of channels in schedule */
    sched_type::size_type nchannels(void) const
    {
        return schedule_.size();
    }

    /** @brief Return number of slots in schedule */
    slot_type::size_type nslots(void) const
    {
        if (schedule_.size() == 0)
            return 0;

        return schedule_[0].size();
    }

    /** @brief Return slot schedule at given channel */
    sched_type::const_reference operator [](sched_type::size_type chan) const
    {
        return schedule_[chan];
    }

    /** @brief Return true if we can transmit in given slot */
    bool canTransmitInSlot(size_t slot) const
    {
        for (size_t chan = 0; chan < nchannels(); ++chan) {
            if (schedule_[chan][slot])
                return true;
        }

        return false;
    }

    /** @brief Return true if we can transmit on given channel (in any slot) */
    bool canTransmitOnChannel(size_t chan) const
    {
        for (size_t slot = 0; slot < nslots(); ++slot) {
            if (schedule_[chan][slot])
                return true;
        }

        return false;
    }

    /** @brief Find the index of the first channel on which we can transmit in
     * the given slot.
     */
    std::optional<size_t> firstChannelIdx(size_t slot) const
    {
        for (size_t chan = 0; chan < nchannels(); ++chan) {
            if (schedule_[chan][slot])
                return chan;
        }

        return std::nullopt;
    }

    /** @brief Is this an FDMA schedule? */
    bool isFDMA(void) const
    {
        for (size_t chan = 0; chan < nchannels(); ++chan) {
            const slot_type &slots = schedule_[chan];

            if (!std::equal(slots.begin() + 1, slots.end(), slots.begin()))
                return false;
        }

        return true;
    }

    /** @brief May we overfill given slot? */
    bool mayOverfill(size_t chan, size_t slot) const
    {
        return superslots_ && schedule_[chan][(slot + 1) % nslots()];
    }

    /** @brief Determine slot at given time
     * @brief t Time
     * @return slot
     */
    template<class Clock>
    size_t slotAt(const std::chrono::time_point<Clock> &t) const
    {
        return (t.time_since_epoch() % frame_size_) / slot_size_;
    }

    /** @brief Determine slot offset at given time
     * @brief t Time
     * @return offset into current slot
     */
    template<class Clock>
    std::chrono::duration<double> slotOffsetAt(const std::chrono::time_point<Clock> &t) const
    {
        return t.time_since_epoch() % slot_size_;
    }

private:
    /** @brief The slot schedule */
    sched_type schedule_;

    /** @brief The slot size */
    std::chrono::duration<double> slot_size_;

    /** @brief The guard size */
    std::chrono::duration<double> guard_size_;

    /** @brief The frame size */
    std::chrono::duration<double> frame_size_;

    /** @brief Allow merged adjacent slots ("superslots") */
    bool superslots_;

    /** @brief Validate the slot schedule */
    void validate(void)
    {
        if (schedule_.size() == 0)
            throw std::out_of_range("Schedule has no channels");

        // Check that all channels have the same number of slot
        size_t nslots = schedule_[0].size();

        for (size_t chan = 1; chan < schedule_.size(); ++chan) {
            if (schedule_[chan].size() != nslots)
                throw std::out_of_range("Schedule channels have differing numbers of slots");
        }

        // Update frame size
        frame_size_ = nslots*slot_size_;
    }
};

#endif /* SCHEDULE_H_ */
