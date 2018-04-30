#include <functional>

#include "ParallelPacketDemodulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<NET> net,
                                                     std::shared_ptr<PHY> phy,
                                                     unsigned int nthreads) :
    net(net),
    phy(phy),
    workQueue(nthreads, net, phy)
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

ParallelPacketDemodulator::Worker::Worker(std::shared_ptr<NET> net,
                                          std::shared_ptr<PHY> phy) :
    _net(net),
    _demod(phy->make_demodulator())
{
}

void ParallelPacketDemodulator::Worker::operator ()(std::unique_ptr<IQQueue>& buf)
{
    _demod->demodulate(std::move(buf), _net->sendQueue);
}
