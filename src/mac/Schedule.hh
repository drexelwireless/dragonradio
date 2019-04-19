#ifndef SCHEDULE_H_
#define SCHEDULE_H_

#include <vector>

/** @brief A schedule specifying the channels on which a node may transmit
 * in a given slot.
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
        schedule_ = schedule;

        return *this;
    }

    sched_type::size_type size(void) const
    {
        return schedule_.size();
    }

    sched_type::const_reference operator [](slot_type::size_type i) const
    {
        return schedule_[i];
    }

    /** @brief Return true if we can transmit in given slot */
    bool canTransmit(size_t slot) const
    {
        for (size_t chan = 0; chan < schedule_.size(); ++chan) {
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
         for (size_t chan = 0; chan < schedule_.size(); ++chan) {
             if (schedule_[chan][slot]) {
                 chan_ = chan;
                 return true;
             }
         }

         return false;
     }

private:
    /** @brief The slot schedule */
    sched_type schedule_;
};

#endif /* SCHEDULE_H_ */
