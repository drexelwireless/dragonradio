#ifndef SYNTHESIZER_H_
#define SYNTHESIZER_H_

#include <atomic>

#include "spinlock_mutex.hh"
#include "Logger.hh"
#include "mac/Schedule.hh"
#include "net/Net.hh"
#include "phy/PHY.hh"

/** @brief Base class for synthesizers */
class Synthesizer : public Element
{
public:
    Synthesizer(std::shared_ptr<PHY> phy,
                double tx_rate,
                const Channels &channels)
      : sink(*this, nullptr, nullptr)
      , phy_(phy)
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
        std::lock_guard<spinlock_mutex> lock(mutex_);

        tx_rate_ = rate;
        reconfigure();
    }

    /** @brief Get channels. */
    virtual const Channels &getChannels(void) const
    {
        return channels_;
    }

    /** @brief Set channels */
    virtual void setChannels(const Channels &channels)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

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
        std::lock_guard<spinlock_mutex> lock(mutex_);

        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

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
    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Mutex for synthesizer state. */
    spinlock_mutex mutex_;

    /** @brief TX sample rate */
    double tx_rate_;

    /** @brief Radio channels */
    Channels channels_;

    /** @brief Radio schedule */
    Schedule schedule_;
};

/** @brief Modulate packets for a channel. */
/** This class is responsible for modulating packets and synthesizing a channel
 * from the modulated packet.
 */
class ChannelModulator {
public:
    ChannelModulator(PHY &phy,
                     unsigned chanidx,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double tx_rate)
      : chanidx_(chanidx)
      , channel_(channel)
      // XXX Protected against channel with zero bandwidth
      , rate_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw))
      , fshift_(channel.fc/tx_rate)
      , mod_(phy.mkPacketModulator())
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
    /** @brief Index of channel we are modulating */
    const unsigned chanidx_;

    /** @brief Channel we are modulating */
    const Channel channel_;

    /** @brief Resampling rate */
    const double rate_;

    /** @brief Frequency shift */
    const double fshift_;

    /** @brief Packet modulator */
    std::shared_ptr<PHY::PacketModulator> mod_;
};

#endif /* SYNTHESIZER_H_ */
