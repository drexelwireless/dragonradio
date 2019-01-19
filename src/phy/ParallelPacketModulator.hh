#ifndef PARALLELPACKETMODULATOR_H_
#define PARALLELPACKETMODULATOR_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "PacketModulator.hh"
#include "liquid/Resample.hh"
#include "phy/Channel.hh"
#include "phy/ChannelModulator.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A parallel packet modulator. */
class ParallelPacketModulator : public PacketModulator, public Element
{
public:
    ParallelPacketModulator(std::shared_ptr<Net> net,
                            std::shared_ptr<PHY> phy,
                            const Channel &tx_channel,
                            size_t nthreads);
    virtual ~ParallelPacketModulator();

    double getMaxTXUpsampleRate(void) override;

    virtual void modulateOne(std::shared_ptr<NetPacket> pkt,
                             ModPacket &mpkt) override;

    void modulate(size_t n) override;

    size_t pop(std::list<std::unique_ptr<ModPacket>>& pkts,
               size_t maxSamples,
               bool overfill) override;

    void reconfigure(void) override;

    /** @brief Get TX channel. */
    Channel getTXChannel(void)
    {
        return tx_channel_;
    }

    /** @brief Set TX channel. */
    void setTXChannel(const Channel &channel)
    {
        tx_channel_ = channel;
        reconfigure();
    }

    /** @brief Get prototype filter for channelization. */
    const std::vector<C> &getTaps(void) const
    {
        return taps_;
    }

    /** @brief Set prototype filter for channelization. */
    /** The prototype filter should have unity gain. */
    void setTaps(const std::vector<C> &taps)
    {
        taps_ = taps;
        reconfigure();
    }

    /** @brief Stop modulating. */
    void stop(void);

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

private:
    /** @brief Our network. */
    std::shared_ptr<Net> net_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Prototype filter */
    std::vector<C> taps_;

    /** @brief TX channel */
    Channel tx_channel_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Thread running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Number of modulated samples we want. */
    size_t nwanted_;

    /** @brief Number of modulated samples we have */
    size_t nsamples_;

    /** @brief Mutex to serialize access to the network */
    std::mutex net_mutex_;

    /* @brief Mutex protecting queue of modulated packets */
    std::mutex pkt_mutex_;

    /* @brief Condition variable used to signal modulation workers */
    std::condition_variable producer_cond_;

    /* @brief Queue of modulated packets */
    std::list<std::unique_ptr<ModPacket>> pkt_q_;

    /* @brief Modulator for one-off modulation */
    ChannelModulator one_mod_;

    /** @brief Get TX upsample rate. */
    double getTXUpsampleRate(void)
    {
        if (tx_channel_.bw == 0.0)
            return 1.0;
        else
            return tx_rate_/(phy_->getMinRXRateOversample()*tx_channel_.bw);
    }

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig);
};

#endif /* PARALLELPACKETMODULATOR_H_ */
