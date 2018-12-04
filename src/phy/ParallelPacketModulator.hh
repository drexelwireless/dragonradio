#ifndef PARALLELPACKETMODULATOR_H_
#define PARALLELPACKETMODULATOR_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "PacketModulator.hh"
#include "liquid/Resample.hh"
#include "phy/ModParams.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A parallel packet modulator. */
class ParallelPacketModulator : public PacketModulator, public Element
{
public:
    ParallelPacketModulator(std::shared_ptr<Net> net,
                            std::shared_ptr<PHY> phy,
                            const Channels &channels,
                            size_t nthreads);
    virtual ~ParallelPacketModulator();

   virtual void modulateOne(std::shared_ptr<NetPacket> pkt,
                            ModPacket &mpkt) override;

    void modulate(size_t n) override;

    size_t pop(std::list<std::unique_ptr<ModPacket>>& pkts,
               size_t maxSamples,
               bool overfill) override;

    void reconfigure(void) override;

    /** @brief Stop modulating. */
    void stop(void);

    /** @brief Input port for packets. */
    NetIn<Pull> sink;

    /** @brief Resampler parameters for modulator */
    Liquid::ResamplerParams upsamp_params;

private:
    /** @brief Our network. */
    std::shared_ptr<Net> net_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

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
    std::queue<std::unique_ptr<ModPacket>> pkt_q_;

    /* @brief Modulator for one-off modulation */
    std::shared_ptr<PHY::Modulator> one_mod_;

    /* @brief Parameters for one-off modulation */
    ModParams one_modparams_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig);

    /** @brief Modulate one packet with given parameters */
    void modulateWithParams(PHY::Modulator &modulator,
                            ModParams &params,
                            std::shared_ptr<NetPacket> pkt,
                            ModPacket &mpkt);
};

#endif /* PARALLELPACKETMODULATOR_H_ */
