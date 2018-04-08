#ifndef MODQUEUE_H_
#define MODQUEUE_H_

#include <condition_variable>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PHY.hh"

class ModQueue
{
public:
    //functions
    ModQueue(std::shared_ptr<NET> net,
             std::shared_ptr<PHY> phy);
    ~ModQueue();

    void join(void);

    size_t getWatermark(void);

    void setWatermark(size_t watermark);

    /** Pop a modulated packet, but only if it consist of maxSamples sampels or
        fewer */
    std::unique_ptr<ModPacket> pop(size_t maxSamples);

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

#endif /* MODQUEUE_H_ */
