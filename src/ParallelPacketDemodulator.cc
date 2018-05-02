#include <functional>

#include "ParallelPacketDemodulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<NET> net,
                                                     std::shared_ptr<PHY> phy,
                                                     std::shared_ptr<Logger> logger,
                                                     unsigned int nthreads) :
    net(net),
    phy(phy),
    logger(logger),
    _prev_samps(0),
    _cur_samps(0),
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

void ParallelPacketDemodulator::setDemodParameters(const size_t prev_samps,
                                                   const size_t cur_samps)
{
    _prev_samps = prev_samps;
    _cur_samps = cur_samps;
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
            net->send(std::move(pkt));
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

        // Calculate how many samples we want to demodulate from the tail end of
        // the previous slot
        size_t buf1_nsamples = buf1->oversample + _prev_samps;

        if (buf1_nsamples > buf1->size())
            // Should never happen!
            buf1_nsamples = buf1->size();

        // Reset the state of the demodulator
        demod->reset(buf1->timestamp, buf1->size() - buf1_nsamples);

        // Demodulate the last part of the guard interval of the previous slots
        demod->demodulate(buf1->data() + buf1->size() - buf1_nsamples, buf1_nsamples, callback);

        // Calculate how many samples from the current slot we want to
        // demodulate. We do not demodulate the tail end of the guard interval.
        size_t ndemodulated = 0; // How many samples we've already demodulated
        size_t nwanted;          // How many samples we still want to demodulate.
        size_t n = 0;

        nwanted = _cur_samps - buf2->undersample;

        for (;;) {
            n = std::min(buf2->nsamples.load(std::memory_order_acquire) - ndemodulated, nwanted);

            demod->demodulate(&(*buf2)[ndemodulated], n, callback);
            ndemodulated += n;
            nwanted -= n;

            if (buf2->complete)
                break;
        }

        // If we received any packets, log both slots.
        if (logger && received) {
            logger->logSlot(buf1);
            logger->logSlot(buf2);
        }
    }
}
