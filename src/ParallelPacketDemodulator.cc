#include <functional>

#include "ParallelPacketDemodulator.hh"
#include "NET.hh"
#include "PHY.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<NET> net,
                                                     std::shared_ptr<PHY> phy,
                                                     unsigned int nthreads) :
    net(net),
    phy(phy),
    workQueue(nthreads, *phy)
{
}

ParallelPacketDemodulator::~ParallelPacketDemodulator()
{
}

void ParallelPacketDemodulator::stop(void)
{
    workQueue.stop();
}

void ParallelPacketDemodulator::push(std::unique_ptr<IQQueue> buf)
{
    workQueue.submit(std::move(buf));
}
