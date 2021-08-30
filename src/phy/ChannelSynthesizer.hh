// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CHANNELSYNTHESIZER_HH_
#define CHANNELSYNTHESIZER_HH_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include "phy/Channel.hh"
#include "phy/ModPacketQueue.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief A single-channel synthesizer. */
class ChannelSynthesizer : public Synthesizer
{
public:
    using container_type = ModPacketQueue<>::container_type;

    ChannelSynthesizer(double tx_rate,
                       const std::vector<PHYChannel> &channels)
      : Synthesizer(tx_rate, channels)
    {
    }

    virtual ~ChannelSynthesizer() = default;

    /** @brief Get high-water mark. */
    std::optional<size_t> getHighWaterMark(void) const
    {
        return queue_.getHighWaterMark();
    }

    /** @brief Set high-water mark. */
    void setHighWaterMark(std::optional<size_t> mark)
    {
        queue_.setHighWaterMark(mark);
    }

    /** @brief Pop modulated packets. */
    size_t try_pop(container_type &mpkts, size_t max_samples, bool overfill)
    {
        return queue_.try_pop(mpkts, max_samples, overfill);
    }

    /** @brief Pop all modulated packets. */
    size_t try_pop(container_type &mpkts)
    {
        return queue_.try_pop(mpkts);
    }

    /** @brief Pop all modulated packets. */
    size_t pop(container_type &mpkts)
    {
        return queue_.pop(mpkts);
    }

protected:
    /** @brief Index of channel we should synthesize. */
    std::optional<size_t> chanidx_;

    /** @brief Queue */
    ModPacketQueue<> queue_;
};

#endif /* CHANNELSYNTHESIZER_HH_ */
