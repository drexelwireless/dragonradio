// Copyright 2018-2022 Drexel University
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

/** @brief A record of transmitted packets */
struct TXRecord {
    TXRecord()
      : delay(0)
      , nsamples(0)
    {
    }

    TXRecord(const TXRecord&) = delete;

    TXRecord(TXRecord &&other)
      : timestamp(std::move(other.timestamp))
      , delay(other.delay)
      , nsamples(other.nsamples)
      , iqbufs(std::move(other.iqbufs))
      , mpkts(std::move(other.mpkts))
    {
        other.delay = 0;
        other.nsamples = 0;
    }

    // So we can emplace
    TXRecord(const std::optional<MonoClock::time_point>& timestamp_,
             size_t delay_,
             size_t nsamples_,
             std::list<std::shared_ptr<IQBuf>>&& iqbufs_,
             std::list<std::unique_ptr<ModPacket>>&& mpkts_) noexcept
      : timestamp(timestamp_)
      , delay(delay_)
      , nsamples(nsamples_)
      , iqbufs(std::move(iqbufs_))
      , mpkts(std::move(mpkts_))
    {
    }

    TXRecord& operator =(const TXRecord&) = delete;

    TXRecord& operator =(TXRecord &&other)
    {
        timestamp = std::move(other.timestamp);
        delay = other.delay;
        nsamples = other.nsamples;
        iqbufs = std::move(other.iqbufs);
        mpkts = std::move(other.mpkts);

        other.delay = 0;
        other.nsamples = 0;

        return *this;
    }

    /** @brief TX deadline */
    std::optional<MonoClock::time_point> timestamp;

    /** @brief Number of samples from timestamp transmission was delayed */
    size_t delay;

    /** @brief Number of samples transmitted */
    size_t nsamples;

    /** @brief Transmitted IQ buffers */
    std::list<std::shared_ptr<IQBuf>> iqbufs;

    /** @brief Transmitted modulated packets */
    std::list<std::unique_ptr<ModPacket>> mpkts;
};

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
    virtual double getTXRate(void) const
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
      , rate_(tx_rate/channel.channel.bw)
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
