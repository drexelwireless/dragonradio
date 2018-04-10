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
        Worker(PHY& phy, NET& net) : net(net), demod(phy.make_demodulator()) {}
        ~Worker() {}

        Worker(const Worker&) = delete;
        Worker(Worker&& other) : net(other.net), demod(std::move(other.demod)), pkts(std::move(other.pkts)) {}

        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        static std::unique_ptr<Worker> make_worker(PHY& phy, NET& net)
        {
            return std::make_unique<Worker>(phy, net);
        }

        void operator ()(std::unique_ptr<IQQueue>& buf)
        {
            demod.demodulate(std::move(buf), pkts);

            while (!pkts.empty()) {
                net.sendPacket(std::move(pkts.front()));

                pkts.pop();
            }
        }

    private:
        NET& net;
        Demodulator demod;
        std::queue<std::unique_ptr<RadioPacket>> pkts;
    };

public:
    ParallelPacketDemodulator(std::shared_ptr<NET> net,
                              std::shared_ptr<PHY> phy,
                              unsigned int nthreads);

    ~ParallelPacketDemodulator();

    void stop(void);

    void push(std::unique_ptr<IQQueue> buf);

private:
    std::shared_ptr<NET> net;
    std::shared_ptr<PHY> phy;

    /** Work queue for demodulating packets */
    WorkQueue<std::unique_ptr<IQQueue>, Worker> workQueue;

    /** Function to create a demodulation worker */
    std::function<void(std::unique_ptr<IQQueue>&)> mkDemodWorker(void);

    /** Demodulation worker */
    void demodWorker(std::unique_ptr<Demodulator> demod, std::unique_ptr<IQQueue>& buf);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
