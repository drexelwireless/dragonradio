// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include <atomic>
#include <mutex>

#include "sync_barrier.hh"
#include "Logger.hh"
#include "mac/Schedule.hh"
#include "net/Element.hh"
#include "phy/PHY.hh"

/** @brief Base class for synthesizers */
class Synthesizer : public Element, protected sync_barrier
{
public:
    Synthesizer(const std::vector<PHYChannel> &channels,
                double tx_rate,
                unsigned nsyncthreads)
      : sync_barrier(nsyncthreads)
      , sink(*this, nullptr, nullptr)
      , channels_(channels)
      , tx_rate_(tx_rate)
    {
    }

    virtual ~Synthesizer() = default;

    /** @brief Get channels. */
    virtual std::vector<PHYChannel> getChannels(void) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const std::vector<PHYChannel> &channels)
    {
        modify([&]() { channels_ = channels; reconfigure(); });
    }

    /** @brief Get the TX sample rate. */
    virtual double getTXRate(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return tx_rate_;
    }

    /** @brief Set the TX sample rate.
     * @param rate The rate.
     */
    virtual void setTXRate(double rate)
    {
        modify([&]() { tx_rate_ = rate; reconfigure(); }, [&](){ return tx_rate_ != rate; });
    }

    /** @brief Get schedule. */
    virtual const Schedule &getSchedule(void) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return schedule_;
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule &schedule)
    {
        modify([&]() { schedule_ = schedule; reconfigure(); });
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        modify([&]() { schedule_ = schedule; reconfigure(); });
    }

    /** @brief Stop modulating. */
    virtual void stop(void) = 0;

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

protected:
    /** @brief Radio channels */
    std::vector<PHYChannel> channels_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Radio schedule */
    Schedule schedule_;

    /** @brief Reconfigure for new parameters */
    virtual void reconfigure(void);

    void wake_dependents(void) override;
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
      , rate_(channel.channel.bw == 0.0 ? 1.0 : tx_rate/(channel.phy->getTXOversampleFactor()*channel.channel.bw))
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
