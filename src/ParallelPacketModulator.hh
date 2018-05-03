#ifndef PARALLELPACKETMODULATOR_H_
#define PARALLELPACKETMODULATOR_H_

#include <condition_variable>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PacketModulator.hh"
#include "phy/PHY.hh"

/** @brief A parallel packet modulator. */
class ParallelPacketModulator : public PacketModulator
{
public:
    ParallelPacketModulator(std::shared_ptr<NET> net,
                            std::shared_ptr<PHY> phy);
    virtual ~ParallelPacketModulator();

    size_t getWatermark(void) override;

    void setWatermark(size_t watermark) override;

    std::unique_ptr<ModPacket> pop(size_t maxSamples) override;

    /** @brief Stop modulating. */
    void stop(void);

private:
    std::shared_ptr<NET> net;
    std::shared_ptr<PHY> phy;

    /** Flag indicating if we should stop processing packets */
    bool done;

    /** Thread running modWorker */
    std::thread modThread;

    /** Thread modulating packets */
    void modWorker(void);

    /** Number of modulated samples we want to have on-hand at all times */
    size_t watermark;

    /** Number of modulated samples we have */
    size_t nsamples;

    /** Modulated radio packets */
    std::mutex                             m;
    std::condition_variable                prod;
    std::condition_variable                cons;
    std::queue<std::unique_ptr<ModPacket>> q;
};

#endif /* PARALLELPACKETMODULATOR_H_ */
