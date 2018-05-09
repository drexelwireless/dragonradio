#ifndef PARALLELPACKETMODULATOR_H_
#define PARALLELPACKETMODULATOR_H_

#include <condition_variable>
#include <mutex>
#include <queue>

#include "PacketModulator.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A parallel packet modulator. */
class ParallelPacketModulator : public PacketModulator
{
public:
    ParallelPacketModulator(std::shared_ptr<Net> net,
                            std::shared_ptr<PHY> phy,
                            size_t nthreads);
    virtual ~ParallelPacketModulator();

    size_t getWatermark(void) override;

    void setWatermark(size_t watermark) override;

    void pop(std::list<std::unique_ptr<ModPacket>>& pkts, size_t maxSamples);

    /** @brief Stop modulating. */
    void stop(void);

private:
    /** @brief Our network. */
    std::shared_ptr<Net> net;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy;

    /** @brief Flag indicating if we should stop processing packets */
    bool done;

    /** @brief Thread running modWorker */
    std::vector<std::thread> mod_threads;

    /** @brief Thread modulating packets */
    void mod_worker(void);

    /** @brief Number of modulated samples we want to have on-hand at all times. */
    size_t watermark;

    /** @brief Number of modulated samples we have */
    size_t nsamples;

    /** @brief Mutex to serialize access to the network */
    std::mutex net_mutex;

    /* @brief Mutex protecting queue of modulated packets */
    std::mutex m;

    /* @brief Condition variable used to signal modulation workers */
    std::condition_variable prod;

    /* @brief Queue of modulated packets */
    std::queue<std::unique_ptr<ModPacket>> q;
};

#endif /* PARALLELPACKETMODULATOR_H_ */
