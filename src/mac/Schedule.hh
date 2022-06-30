// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <vector>

/** @brief A schedule specifying the channels on which a node may transmit in a
 * given slot.
 */
class Schedule {
public:
    using slot_type = std::vector<bool>;
    using sched_type = std::vector<slot_type>;

    Schedule() = default;
    Schedule(const Schedule&) = default;
    Schedule(Schedule&&) = default;

    Schedule& operator=(const Schedule&) = default;
    Schedule& operator=(Schedule&&) = default;

    Schedule& operator=(const sched_type& schedule)
    {
        if (schedule.size() == 0)
            throw std::out_of_range("Schedule has no channels");

        // Check that all channels have the same number of slot
        size_t nslots = schedule[0].size();

        for (size_t chan = 1; chan < schedule.size(); ++chan) {
            if (schedule[chan].size() != nslots)
                throw std::out_of_range("Schedule channels have differing numbers of slots");
        }

        schedule_ = schedule;

        return *this;
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

    sched_type::const_reference operator [](slot_type::size_type i) const
    {
        return schedule_[i];
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

    /** @brief Find the first channel index in which we can transmit in the
     * given slot.
     */
    bool firstChannelIdx(size_t slot,
                         size_t &chan_) const
    {
        for (size_t chan = 0; chan < nchannels(); ++chan) {
            if (schedule_[chan][slot]) {
                chan_ = chan;
                return true;
            }
        }

        return false;
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

private:
    /** @brief The slot schedule */
    sched_type schedule_;
};

#endif /* SCHEDULE_H_ */
