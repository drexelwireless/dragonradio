// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MAC_H_
#define MAC_H_

#include <deque>
#include <memory>

#include "USRP.hh"
#include "llc/Controller.hh"
#include "mac/MAC.hh"
#include "mac/Snapshot.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief A MAC protocol. */
class MAC
{
public:
    /** @brief MAC load, measured as samples transmitted over time */
    struct Load {
        /** @brief Start of load measurement period */
        WallClock::time_point start;

        /** @brief End of load measurement period */
        WallClock::time_point end;

        /** @brief Load per channel measured in number of samples */
        std::vector<size_t> nsamples;

        void reset(size_t nchannels)
        {
            start = WallClock::now();
            nsamples.resize(nchannels);
            std::fill(nsamples.begin(), nsamples.end(), 0);
        }
    };

    MAC(std::shared_ptr<USRP> usrp,
        std::shared_ptr<PHY> phy,
        std::shared_ptr<Controller> controller,
        std::shared_ptr<SnapshotCollector> collector,
        std::shared_ptr<Channelizer> channelizer,
        std::shared_ptr<Synthesizer> synthesizer,
        double rx_period);
    virtual ~MAC() = default;

    MAC() = delete;
    MAC(const MAC&) = delete;
    MAC& operator =(const MAC&) = delete;
    MAC& operator =(MAC&&) = delete;

    /** @brief Get the MAC's channelizer */
    const std::shared_ptr<Channelizer> &getChannelizer(void)
    {
        return channelizer_;
    }

    /** @brief Get the MAC's synthesizer */
    const std::shared_ptr<Synthesizer> &getSynthesizer(void)
    {
        return synthesizer_;
    }

    /** @brief Can this MAC transmit
     * @return true if we can transmit, false otherwise
     */
    virtual bool canTransmit(void) const
    {
        return can_transmit_;
    }

    /** @brief Get MAC schedule */
    virtual const Schedule &getSchedule(void) const
    {
        return schedule_;
    }

    /** @brief Set MAC schedule */
    virtual void setSchedule(const Schedule &schedule)
    {
        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set MAC schedule */
    virtual void setSchedule(const Schedule::sched_type &schedule)
    {
        schedule_ = schedule;
        reconfigure();
    }

    /** @brief Set minimum channel bandwidth */
    virtual void setMinChannelBandwidth(double min_bw)
    {
        min_chan_bw_ = min_bw;
        reconfigure();
    }

    /** @brief Get current load */
    Load getLoad(void)
    {
        Load load;

        {
            std::lock_guard<spinlock_mutex> lock(load_mutex_);

            load = load_;
            load.end = std::max(load.end, WallClock::now());
        }

        return load;
    }

    /** @brief Get current load and reset load counters */
    Load popLoad(void)
    {
        Load load;

        {
            std::lock_guard<spinlock_mutex> lock(load_mutex_);

            load = load_;
            load.end = std::max(load.end, WallClock::now());
            if (synthesizer_)
                load_.reset(synthesizer_->getChannels().size());
            else
                load_.reset(0);
        }

        return load;
    }

    /** @brief Reconfigure the MAC when after parameters change */
    virtual void reconfigure(void);

    /** @brief Stop processing packets. */
    virtual void stop(void) = 0;

protected:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> usrp_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Our controller */
    std::shared_ptr<Controller> controller_;

    /** @brief Our snapshot collector */
    std::shared_ptr<SnapshotCollector> snapshot_collector_;

    /** @brief Our channelizer. */
    std::shared_ptr<Channelizer> channelizer_;

    /** @brief Our synthesizer. */
    std::shared_ptr<Synthesizer> synthesizer_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Flag indicating whether or not we can transmit */
    /** This is used, in particular, for the TDMA MAC, which may not have a slot
     */
    bool can_transmit_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;

    /** @brief Length of RX period (sec) */
    double rx_period_;

    /** @brief Number of RX samples in an RX period */
    size_t rx_period_samps_;

    /** @brief RX buffer size */
    size_t rx_bufsize_;

    /** @brief The MAC schedule */
    Schedule schedule_;

    /** @brief The minimum channel bandwidth (Hz) */
    double min_chan_bw_;

    /** @brief TX center frequency offset from RX center frequency. */
    /** If the TX and RX rates are different, this is non-empty and contains
     * the frequency of the channel we transmit on.
     */
    std::optional<double> tx_fc_off_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Mutex for load */
    spinlock_mutex load_mutex_;

    /** @brief Number of sent samples */
    Load load_;

    /** @brief A transmission record */
    struct TXRecord {
        /** @brief TX deadline */
        WallClock::time_point deadline;

        /** @brief Number of samples the deadline was delayed */
        size_t deadline_delay;

        /** @brief Number of samples transmitted */
        size_t nsamples;

        /** @brief Transmitted IQ buffers */
        std::list<std::shared_ptr<IQBuf>> iqbufs;

        /** @brief Transmitted modulated packets */
        std::list<std::unique_ptr<ModPacket>> mpkts;
    };

    /** @brief Mutex for transmission records */
    std::mutex tx_records_mutex_;

    /** @brief Condition variable protecting transmission records */
    std::condition_variable tx_records_cond_;

    /** @brief Queue of transmission records */
    std::queue<TXRecord> tx_records_;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Worker handling notification for transmitted slots */
    void txNotifier(void);
};

#endif /* MAC_H_ */
