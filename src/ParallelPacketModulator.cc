#include "ParallelPacketModulator.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

ParallelPacketModulator::ParallelPacketModulator(std::shared_ptr<Net> net,
                                                 std::shared_ptr<PHY> phy,
                                                 size_t nthreads) :
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
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads.emplace_back(std::thread(&ParallelPacketModulator::mod_worker, this));
}

ParallelPacketModulator::~ParallelPacketModulator()
{
}

void ParallelPacketModulator::stop(void)
{
    done = true;
    prod.notify_all();

    for (size_t i = 0; i < mod_threads.size(); ++i) {
        if (mod_threads[i].joinable())
            mod_threads[i].join();
    }
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

        while (!q.empty()) {
            ModPacket& mpkt = *(q.front());

            if (mpkt.complete.test_and_set(std::memory_order_acquire))
                break;

            size_t n = mpkt.samples->size();

            if (n > maxSamples) {
                mpkt.complete.clear(std::memory_order_release);
                break;
            }

            pkts.emplace_back(std::move(q.front()));
            q.pop();
            nsamples -= n;
            maxSamples -= n;
        }
    }

    prod.notify_all();
}

void ParallelPacketModulator::mod_worker(void)
{
    std::unique_ptr<PHY::Modulator> modulator = phy->make_modulator();
    std::unique_ptr<NetPacket>      pkt;
    ModPacket                       *mpkt;

    for (;;) {
        // Wait for queue to be below watermark
        {
            std::unique_lock<std::mutex> lock(m);

            prod.wait(lock, [this]{ return done || nsamples < watermark; });
        }

        // Exit if we are done
        if (done)
            break;

        {
            // Get a packet from the network
            std::unique_lock<std::mutex> net_lock(net_mutex);

            pkt = net->recvPacket();

            if (!pkt)
                continue;

            // Now place a ModPacket in our queue. The packet isn't complete
            // yet, but we need to put it in the queue now to ensure that
            // packets are modulated in the order they are received from the
            // network. Note that we acquire the lock on the network first, then
            // the lock on the queue. We don't want to hold the lock on the
            // queue for long because that will starve the transmitter.
            std::unique_lock<std::mutex> lock(m);

            q.emplace(std::make_unique<ModPacket>());
            mpkt = q.back().get();
        }

        pkt->check = _check;
        pkt->fec0 = _fec0;
        pkt->fec1 = _fec1;
        pkt->ms = _ms;
        pkt->g = _g;

        // Modulate the packet
        modulator->modulate(*mpkt, std::move(pkt));

        // Save the number of modulated samples so we can record them later.
        size_t n = mpkt->samples->size();

        // Mark the modulated packet as complete. The packet may be invalidated
        // by a consumer immediately after we mark it complete, so we cannot use
        // the mpkt pointer after this statement!
        mpkt->complete.clear(std::memory_order_release);

        // Add the number of modulated samples to the total in the queue. Note
        // that the packet may already have been removed from the queue and the
        // number of samples it contains subtracted from nsamples, in which case
        // we are merely restoring the universe to its rightful balance post
        // hoc.
        {
            std::lock_guard<std::mutex> lock(m);

            nsamples += n;
        }
    }
}
