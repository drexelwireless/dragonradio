// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "Logger.hh"
#include "RadioNet.hh"
#include "mac/Schedule.hh"
#include "phy/PHY.hh"

/** @brief Base class for synthesizers */
class Synthesizer : public Element
{
public:
    Synthesizer(double tx_rate,
                const std::vector<PHYChannel> &channels)
      : sink(*this, nullptr, nullptr)
      , tx_rate_(tx_rate)
      , channels_(channels)
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
        std::lock_guard<std::mutex> lock(mutex_);

        tx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get channels. */
    virtual const std::vector<PHYChannel> &getChannels(void) const
    {
        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const std::vector<PHYChannel> &channels)
    {
        std::lock_guard<std::mutex> lock(mutex_);

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
        std::lock_guard<std::mutex> lock(mutex_);

        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Stop modulating. */
    virtual void stop(void) = 0;

    /** @brief Reconfigure for new TX parameters */
    virtual void reconfigure(void) = 0;

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

protected:
    /** @brief Mutex for synthesizer state. */
    std::mutex mutex_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Radio channels */
    std::vector<PHYChannel> channels_;

    /** @brief Radio schedule */
    Schedule schedule_;
};

/** @brief Modulate packets for a channel. */
/** This class is responsible for modulating packets and synthesizing a channel
 * from the modulated packet.
 */
class ChannelModulator {
public:
    ChannelModulator(const PHYChannel &channel,
                     unsigned chanidx,
                     double tx_rate)
      : channel_(channel)
      , chanidx_(chanidx)
      // XXX Protected against channel with zero bandwidth
      , rate_(channel.channel.bw == 0.0 ? 1.0 : tx_rate/(channel.phy->getMinTXRateOversample()*channel.channel.bw))
      , fshift_(channel.channel.fc/tx_rate)
      , mod_(channel.phy->mkPacketModulator())
    {
    }

    ChannelModulator() = delete;

    virtual ~ChannelModulator() = default;

    /** @brief Modulate a packet to produce IQ samples.
     * @param pkt The NetPacket to modulate.
     * @param g Gain to apply.
     * @param mpkt The ModPacket in which to place modulated samples.
     */
    virtual void modulate(std::shared_ptr<NetPacket> pkt,
                          float g,
                          ModPacket &mpkt) = 0;

protected:
    /** @brief Channel we are modulating */
    const PHYChannel channel_;

    /** @brief Index of channel we are modulating */
    const unsigned chanidx_;

    /** @brief Resampling rate */
    const double rate_;

    /** @brief Frequency shift */
    const double fshift_;

    /** @brief Packet modulator */
    std::shared_ptr<PHY::PacketModulator> mod_;
};

#endif /* SYNTHESIZER_H_ */
