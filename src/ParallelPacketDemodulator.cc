#include <functional>

#include "Logger.hh"
#include "ParallelPacketDemodulator.hh"
#include "NET.hh"
#include "phy/PHY.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<NET> net,
                                                     std::shared_ptr<PHY> phy,
                                                     bool order,
                                                     unsigned int nthreads) :
    net(net),
    phy(phy),
    _order(order),
    _prev_samps(0),
    _cur_samps(0),
    done(false),
    demod_q(radio_q)
{
    net_thread = std::thread(&ParallelPacketDemodulator::net_worker, this);

    for (unsigned int i = 0; i < nthreads; ++i)
        demod_threads.emplace_back(std::thread(&ParallelPacketDemodulator::demod_worker, this));
}

ParallelPacketDemodulator::~ParallelPacketDemodulator()
{
}

void ParallelPacketDemodulator::setDemodParameters(const size_t prev_samps,
                                                   const size_t cur_samps)
{
    _prev_samps = prev_samps;
    _cur_samps = cur_samps;
}

void ParallelPacketDemodulator::push(std::shared_ptr<IQBuf> buf)
{
    demod_q.push(buf);
}

void ParallelPacketDemodulator::stop(void)
{
    done = true;

    demod_q.stop();
    radio_q.stop();

    if (net_thread.joinable())
        net_thread.join();

    for (unsigned int i = 0; i < demod_threads.size(); ++i) {
        if (demod_threads[i].joinable())
            demod_threads[i].join();
    }
}

void ParallelPacketDemodulator::demod_worker(void)
{
    auto                      demod = phy->make_demodulator();
    RadioPacketQueue::barrier b;
    std::shared_ptr<IQBuf>    buf1;
    std::shared_ptr<IQBuf>    buf2;
    bool                      received;

    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt) {
            if (_order)
                radio_q.push(b, std::move(pkt));
            else
                net->send(std::move(pkt));
        }
    };

    while (!done) {
        if (!demod_q.pop(b, buf1, buf2))
            break;

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

        // Remove the barrier since we are done producing packets
        radio_q.erase_barrier(b);

        // If we received any packets, log both slots.
        if (logger && received) {
            logger->logSlot(buf1);
            logger->logSlot(buf2);
        }
    }
}

void ParallelPacketDemodulator::net_worker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!done) {
        if (radio_q.pop(pkt))
            net->send(std::move(pkt));
    }
}

IQBufQueue::IQBufQueue(RadioPacketQueue& radio_q) :
    _radio_q(radio_q),
    _done(false),
    _size(0)
{
}

IQBufQueue::~IQBufQueue()
{
}

void IQBufQueue::push(std::shared_ptr<IQBuf> buf)
{
    // Push the packet on the end of the queue
    {
        std::lock_guard<std::mutex> lock(_m);

        _q.push_back(buf);
        ++_size;
    }

    // Signal anyone waiting on the queue
    _cond.notify_one();
}

bool IQBufQueue::pop(RadioPacketQueue::barrier& b,
                     std::shared_ptr<IQBuf>& buf1,
                     std::shared_ptr<IQBuf>& buf2)
{
    // Acquire the previous slot and the current slot, removing the previous
    // slot from the queue since we no longer need it.
    std::unique_lock<std::mutex> lock(_m);

    _cond.wait(lock, [this]{ return _done || _size > 1; });
    if (_done)
        return false;

    b = _radio_q.push_barrier();
    buf1 = std::move(_q.front());
    _q.pop_front();
    --_size;
    buf2 = _q.front();

    return true;
}

void IQBufQueue::stop(void)
{
    _done = true;
    _cond.notify_all();
}
