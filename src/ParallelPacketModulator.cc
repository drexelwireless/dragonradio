#include "ParallelPacketModulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

ParallelPacketModulator::ParallelPacketModulator(std::shared_ptr<NET> net,
                                                 std::shared_ptr<PHY> phy) :
    net(net),
    phy(phy),
    _check(LIQUID_CRC_32),
    _fec0(LIQUID_FEC_CONV_V29),
    _fec1(LIQUID_FEC_RS_M8),
    _ms(LIQUID_MODEM_QPSK),
    _g(1.0),
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

void ParallelPacketModulator::pop(std::list<std::unique_ptr<ModPacket>>& pkts, size_t maxSamples)
{
    {
        std::unique_lock<std::mutex> lock(m);

        while (!q.empty() && q.front()->samples->size() <= maxSamples) {
            size_t n = q.front()->samples->size();

            pkts.emplace_back(std::move(q.front()));
            q.pop();
            nsamples -= n;
            maxSamples -= n;
        }
    }

    prod.notify_all();
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

        pkt->check = _check;
        pkt->fec0 = _fec0;
        pkt->fec1 = _fec1;
        pkt->ms = _ms;
        pkt->g = _g;

        // Modulate the packet
        auto mpkt = std::make_unique<ModPacket>();

        modulator->modulate(*mpkt, std::move(pkt));

        // Put the packet on the queue
        {
            std::lock_guard<std::mutex> lock(m);

            nsamples += mpkt->samples->size();
            q.push(std::move(mpkt));
        }

        cons.notify_one();
    }
}
