// Copyright 2018-2022 Drexel University
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

    ChannelSynthesizer(const std::vector<PHYChannel> &channels,
                       double tx_rate,
                       unsigned nsyncthreads)
      : Synthesizer(channels, tx_rate, nsyncthreads)
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

    /** @brief Enable the queue. */
    void enable(void)
    {
        queue_.enable();
    }

    /** @brief Disable the queue. */
    void disable(void)
    {
        queue_.disable();
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

    void wake_dependents(void) override;

    void reconfigure(void) override;
};

#endif /* CHANNELSYNTHESIZER_HH_ */
