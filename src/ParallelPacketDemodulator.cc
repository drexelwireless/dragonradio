#include <functional>

#include "ParallelPacketDemodulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<NET> net,
                                                     std::shared_ptr<PHY> phy,
                                                     std::shared_ptr<Logger> logger,
                                                     unsigned int nthreads,
                                                     const size_t prev_slop,
                                                     const size_t cur_slop) :
    net(net),
    phy(phy),
    logger(logger),
    prev_slop(prev_slop),
    cur_slop(cur_slop),
    done(false),
    size(0)
{
    for (unsigned int i = 0; i < nthreads; ++i)
        threads.emplace_back(std::thread(&ParallelPacketDemodulator::worker, this));
}

ParallelPacketDemodulator::~ParallelPacketDemodulator()
{
}

void ParallelPacketDemodulator::stop(void)
{
    done = true;
    cond.notify_all();

    for (unsigned int i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable())
            threads[i].join();
    }
}

void ParallelPacketDemodulator::push(std::shared_ptr<IQBuf> buf)
{
    // Push the packet on the end of the queue
    {
        std::lock_guard<std::mutex> lock(m);

        q.push_back(buf);
        ++size;
    }

    // Signal anyone waiting on the queue
    cond.notify_one();
}

void ParallelPacketDemodulator::worker(void)
{
    auto                   demod = phy->make_demodulator();
    std::shared_ptr<IQBuf> buf1;
    std::shared_ptr<IQBuf> buf2;
    bool                   received;
    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt)
            net->sendQueue.push(std::move(pkt));
    };

    while (!done) {
        // Acquire the previous slot and the current slot, removing the
        // previous slot from the queue since we no longer need it.
        {
            std::unique_lock<std::mutex> lock(m);

            cond.wait(lock, [this]{ return done || size > 1; });
            if (done)
                break;

            buf1 = std::move(q.front());
            q.pop_front();
            --size;
            buf2 = q.front();
        }

        received = false;

        size_t buf1_nsamples = buf1->oversample + prev_slop;
        size_t buf2_nsamples = buf2->size() - buf2->oversample - cur_slop;

        // Should never happen!
        if (buf1_nsamples > buf1->size())
            buf1_nsamples = buf1->size();

        // Should never happen!
        if (buf2->oversample + cur_slop > buf2->size())
            buf2_nsamples = 0;

        demod->reset(buf1->timestamp, buf1->size() - buf1_nsamples);
        demod->demodulate(buf1->data() + buf1->size() - buf1_nsamples, buf1_nsamples, callback);
        demod->demodulate(buf2->data(), buf2_nsamples, callback);

        if (logger && received) {
            logger->logSlot(buf1);
            logger->logSlot(buf2);
        }
    }
}
