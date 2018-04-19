#ifndef PARALLELPACKETDEMODULATOR_H_
#define PARALLELPACKETDEMODULATOR_H_

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PHY.hh"
#include "OrderedWorkQueue.hh"

class ParallelPacketDemodulator
{
private:
    class Worker
    {
    public:
        Worker(PHY& phy, NET& net) : net(net), demod(phy.make_demodulator()) {}
        ~Worker() {}

        Worker(const Worker&) = delete;
        Worker(Worker&& other) : net(other.net), demod(std::move(other.demod)) {}

        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        static std::unique_ptr<Worker> make_worker(PHY& phy, NET& net)
        {
            return std::make_unique<Worker>(phy, net);
        }

        std::pair<NET&,std::queue<std::unique_ptr<RadioPacket>>> operator ()(std::unique_ptr<IQQueue>& buf)
        {
            std::queue<std::unique_ptr<RadioPacket>> pkts;

            demod.demodulate(std::move(buf), pkts);

            return std::pair<NET&,std::queue<std::unique_ptr<RadioPacket>>>(net, std::move(pkts));
        }

        static void result(std::pair<NET&,std::queue<std::unique_ptr<RadioPacket>>> res)
        {
            NET& net = res.first;
            std::queue<std::unique_ptr<RadioPacket>>& pkts = res.second;

            while (!pkts.empty()) {
                net.sendPacket(std::move(pkts.front()));

                pkts.pop();
            }
        }

    private:
        NET& net;
        Demodulator demod;
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
    OrderedWorkQueue<std::unique_ptr<IQQueue>, std::pair<NET&,std::queue<std::unique_ptr<RadioPacket>>>, Worker> workQueue;

    /** Function to create a demodulation worker */
    std::function<void(std::unique_ptr<IQQueue>&)> mkDemodWorker(void);

    /** Demodulation worker */
    void demodWorker(std::unique_ptr<Demodulator> demod, std::unique_ptr<IQQueue>& buf);
};

#endif /* PARALLELPACKETDEMODULATOR_H_ */
