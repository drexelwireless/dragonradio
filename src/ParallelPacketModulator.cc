#include "ParallelPacketModulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

ParallelPacketModulator::ParallelPacketModulator(std::shared_ptr<NET> net,
                                                 std::shared_ptr<PHY> phy) :
    net(net),
    phy(phy),
    done(false),
    watermark(0),
    nsamples(0)
{
   modThread = std::thread(&ParallelPacketModulator::modWorker, this);
}

ParallelPacketModulator::~ParallelPacketModulator()
{
}

void ParallelPacketModulator::stop(void)
{
    done = true;
    prod.notify_all();

    if (modThread.joinable())
        modThread.join();
}

size_t ParallelPacketModulator::getWatermark(void)
{
    return watermark;
}

void ParallelPacketModulator::setWatermark(size_t w)
{
    size_t oldWatermark = watermark;

    watermark = w;

    if (w > oldWatermark)
        prod.notify_all();
}

std::unique_ptr<ModPacket> ParallelPacketModulator::pop(size_t maxSamples)
{
    std::unique_ptr<ModPacket>   pkt;
    std::unique_lock<std::mutex> lock(m);

    if (q.empty())
        return nullptr;

    if (q.front()->samples->size() > maxSamples)
        return nullptr;

    pkt = std::move(q.front());
    q.pop();
    nsamples -= pkt->samples->size();
    prod.notify_all();
    return pkt;
}

void ParallelPacketModulator::modWorker(void)
{
    std::unique_ptr<PHY::Modulator> modulator = phy->make_modulator();

    for (;;) {
        // Wait for queue to be below watermark
        {
            std::unique_lock<std::mutex> lock(m);

            prod.wait(lock, [this]{ return done || nsamples < watermark; });
        }

        if (done)
            break;

        // Get a packet from the network
        std::unique_ptr<NetPacket> pkt = net->recvPacket();

        if (not pkt)
            continue;

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = modulator->modulate(std::move(pkt));

        if (not mpkt)
            continue;

        // Put the packet on the queue
        std::lock_guard<std::mutex> lock(m);

        nsamples += mpkt->samples->size();
        q.push(std::move(mpkt));
        cons.notify_one();
    }
}
