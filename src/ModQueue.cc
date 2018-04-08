#include "ModQueue.hh"
#include "NET.hh"
#include "PHY.hh"

ModQueue::ModQueue(std::shared_ptr<NET> net,
                   std::shared_ptr<PHY> phy)
 : net(net), phy(phy),
   done(false),
   watermark(0),
   nsamples(0)
{
   modThread = std::thread(&ModQueue::modWorker, this);
}

ModQueue::~ModQueue()
{
}

void ModQueue::join(void)
{
    done = true;
    modThread.join();
}

size_t ModQueue::getWatermark(void)
{
    return watermark;
}

void ModQueue::setWatermark(size_t w)
{
    size_t oldWatermark = watermark;

    watermark = w;

    if (w > oldWatermark) {
        printf("Setting watermark %d %d\n", (int) nsamples, (int) watermark);
        prod.notify_all();
    }
}

std::unique_ptr<ModPacket> ModQueue::pop(size_t maxSamples)
{
    std::unique_ptr<ModPacket>   pkt;
    std::unique_lock<std::mutex> lock(m);

    if (q.empty())
        return nullptr;

    if (q.front()->nsamples > maxSamples)
        return nullptr;

    pkt = std::move(q.front());
    q.pop();
    nsamples -= pkt->nsamples;
    prod.notify_all();
    return pkt;
}

void ModQueue::modWorker(void)
{
    while (!done) {
        // Wait for queue to be below watermark
        {
            std::unique_lock<std::mutex> lock(m);

            prod.wait(lock, [this]{ return nsamples < watermark; });
        }

        // Get a packet from the network
        std::unique_ptr<RadioPacket> pkt = net->recvPacket();

        if (not pkt)
            continue;

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = phy->modPkt(std::move(pkt));

        if (not mpkt)
            continue;

        // Put the packet on the queue
        std::lock_guard<std::mutex> lock(m);

        nsamples += mpkt->nsamples;
        q.push(std::move(mpkt));
        cons.notify_one();
    }
}
