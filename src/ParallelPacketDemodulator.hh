#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PHY.hh"
#include "WorkQueue.hh"

class ParallelPacketDemodulator
{
private:
    class Worker
    {
    public:
        Worker(std::shared_ptr<NET> net,
               std::shared_ptr<PHY> phy);
        ~Worker() {}

        Worker(const Worker&) = delete;
        Worker(Worker&& other) = delete;

        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        void operator ()(std::unique_ptr<IQQueue>& buf);

    private:
        /** @brief Destination for packets. */
        std::shared_ptr<NET> _net;

        /** @brief Our demodulator. */
        std::unique_ptr<PHY::Demodulator> _demod;
    };

public:
    ParallelPacketDemodulator(std::shared_ptr<NET> net,
                              std::shared_ptr<PHY> phy,
                              unsigned int nthreads);
    ~ParallelPacketDemodulator();

    void stop(void);

    void push(std::unique_ptr<IQQueue> buf);

private:
    /** @brief Destination for packets. */
    std::shared_ptr<NET> net;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy;

    /** Work queue for demodulating packets */
    WorkQueue<Worker, std::unique_ptr<IQQueue>> workQueue;
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
