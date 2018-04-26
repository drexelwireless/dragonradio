#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PHY.hh"
#include "RadioPacketSink.hh"
#include "WorkQueue.hh"

class ParallelPacketDemodulator
{
private:
    class Worker
    {
    public:
        Worker(PHY& phy) : demod(phy.make_demodulator()) {}
        ~Worker() {}

        Worker(const Worker&) = delete;
        Worker(Worker&& other) = delete;

        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        static std::unique_ptr<Worker> make_worker(PHY& phy)
        {
            return std::make_unique<Worker>(phy);
        }

        void operator ()(std::unique_ptr<IQQueue>& buf)
        {
            demod->demodulate(std::move(buf));
        }

    private:
        std::unique_ptr<PHY::Demodulator> demod;
    };

public:
    ParallelPacketDemodulator(std::shared_ptr<NET> net,
                              std::shared_ptr<PHY> phy,
                              std::shared_ptr<RadioPacketSink> sink,
                              unsigned int nthreads);
    ~ParallelPacketDemodulator();

    void stop(void);

    void push(std::unique_ptr<IQQueue> buf);

private:
    std::shared_ptr<NET> net;
    std::shared_ptr<PHY> phy;

    /** @brief Sink for radio packets. */
    std::shared_ptr<RadioPacketSink> _sink;

    /** Work queue for demodulating packets */
    WorkQueue<std::unique_ptr<IQQueue>, Worker> workQueue;

    /** Function to create a demodulation worker */
    void mkDemodWorker(void);

    /** Demodulation worker */
    void demodWorker(std::unique_ptr<PHY::Demodulator> demod, std::unique_ptr<IQQueue>& buf);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
