#ifndef MAC_H_
#define MAC_H_

#include <deque>
#include <memory>

#include "USRP.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"
#include "mac/Controller.hh"
#include "mac/MAC.hh"
#include "mac/Snapshot.hh"

/** @brief A MAC protocol. */
class MAC
{
public:
    /** @brief MAC load, measured as samples transmitted over time */
    struct Load {
        /** @brief Start of load measurement period */
        Clock::time_point start;

        /** @brief End of load measurement period */
        Clock::time_point end;

        /** @brief Load per channel measured in number of samples */
        std::vector<size_t> nsamples;

        void reset(size_t nchannels)
        {
            start = Clock::now();
            nsamples.resize(nchannels);
            std::fill(nsamples.begin(), nsamples.end(), 0);
        }
    };

    MAC(std::shared_ptr<USRP> usrp,
        std::shared_ptr<PHY> phy,
        std::shared_ptr<Controller> controller,
        std::shared_ptr<SnapshotCollector> collector,
        std::shared_ptr<Channelizer> channelizer,
        std::shared_ptr<Synthesizer> synthesizer);
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

    /** @brief Set minimum channel bandwidth */
    virtual void setMinChannelBandwidth(double min_bw)
    {
    }

    /** @brief Get current load */
    Load getLoad(void)
    {
        Load load;

        {
            std::lock_guard<spinlock_mutex> lock(load_mutex_);

            load = load_;
            load.end = std::max(load.end, Clock::now());
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
            load.end = std::max(load.end, Clock::now());
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

    /** @brief Flag indicating whether or not we can transmit */
    /** This is used, in particular, for the TDMA MAC, which may not have a slot
     */
    bool can_transmit_;

    /** @brief Our channelizer. */
    std::shared_ptr<Channelizer> channelizer_;

    /** @brief Our synthesizer. */
    std::shared_ptr<Synthesizer> synthesizer_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;

    /** @brief Mutex for load */
    spinlock_mutex load_mutex_;

    /** @brief Number of sent samples */
    Load load_;
};

#endif /* MAC_H_ */
