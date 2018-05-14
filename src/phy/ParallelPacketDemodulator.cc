#include <functional>

#include "Logger.hh"
#include "phy/PHY.hh"
#include "phy/ParallelPacketDemodulator.hh"
#include "net/Net.hh"

using namespace std::placeholders;

ParallelPacketDemodulator::ParallelPacketDemodulator(std::shared_ptr<Net> net,
                                                     std::shared_ptr<PHY> phy,
                                                     bool ordered,
                                                     unsigned int nthreads) :
    net_(net),
    phy_(phy),
    ordered_(ordered),
    prev_samps_(0),
    cur_samps_(0),
    done_(false),
    demod_q_(radio_q_)
{
    net_thread_ = std::thread(&ParallelPacketDemodulator::netWorker, this);

    for (unsigned int i = 0; i < nthreads; ++i)
        demod_threads_.emplace_back(std::thread(&ParallelPacketDemodulator::demodWorker, this));
}

ParallelPacketDemodulator::~ParallelPacketDemodulator()
{
    stop();
}

void ParallelPacketDemodulator::setWindowParameters(const size_t prev_samps,
                                                    const size_t cur_samps)
{
    prev_samps_ = prev_samps;
    cur_samps_ = cur_samps;
}

void ParallelPacketDemodulator::push(std::shared_ptr<IQBuf> buf)
{
    demod_q_.push(buf);
}

void ParallelPacketDemodulator::stop(void)
{
    done_ = true;

    demod_q_.stop();
    radio_q_.stop();

    if (net_thread_.joinable())
        net_thread_.join();

    for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
        if (demod_threads_[i].joinable())
            demod_threads_[i].join();
    }
}


bool ParallelPacketDemodulator::getOrdered(void)
{
    return ordered_;
}

void ParallelPacketDemodulator::setOrdered(bool ordered)
{
    ordered_ = ordered;
}

void ParallelPacketDemodulator::demodWorker(void)
{
    auto                      demod = phy_->make_demodulator();
    RadioPacketQueue::barrier b;
    std::shared_ptr<IQBuf>    buf1;
    std::shared_ptr<IQBuf>    buf2;
    bool                      received;

    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt) {
            if (ordered_)
                radio_q_.push(b, std::move(pkt));
            else
                net_->send(std::move(pkt));
        }
    };

    while (!done_) {
        if (!demod_q_.pop(b, buf1, buf2))
            break;

        received = false;

        // Calculate how many samples we want to demodulate from the tail end of
        // the previous slot
        size_t buf1_nsamples = buf1->oversample + prev_samps_;

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

        nwanted = cur_samps_ - buf2->undersample;

        for (;;) {
            n = std::min(buf2->nsamples.load(std::memory_order_acquire) - ndemodulated, nwanted);

            demod->demodulate(&(*buf2)[ndemodulated], n, callback);
            ndemodulated += n;
            nwanted -= n;

            if (buf2->complete)
                break;
        }

        // Remove the barrier since we are done producing packets
        radio_q_.eraseBarrier(b);

        // If we received any packets, log both slots.
        if (logger && received) {
            logger->logSlot(buf1);
            logger->logSlot(buf2);
        }
    }
}

void ParallelPacketDemodulator::netWorker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!done_) {
        if (radio_q_.pop(pkt))
            net_->send(std::move(pkt));
    }
}

IQBufQueue::IQBufQueue(RadioPacketQueue& radio_q) :
    radio_q_(radio_q),
    done_(false),
    size_(0)
{
}

IQBufQueue::~IQBufQueue()
{
}

void IQBufQueue::push(std::shared_ptr<IQBuf> buf)
{
    // Push the packet on the end of the queue
    {
        std::lock_guard<std::mutex> lock(m_);

        q_.push_back(buf);
        ++size_;
    }

    // Signal anyone waiting on the queue
    cond_.notify_one();
}

bool IQBufQueue::pop(RadioPacketQueue::barrier& b,
                     std::shared_ptr<IQBuf>& buf1,
                     std::shared_ptr<IQBuf>& buf2)
{
    // Acquire the previous slot and the current slot, removing the previous
    // slot from the queue since we no longer need it.
    std::unique_lock<std::mutex> lock(m_);

    cond_.wait(lock, [this]{ return done_ || size_ > 1; });
    if (done_)
        return false;

    b = radio_q_.pushBarrier();
    buf1 = std::move(q_.front());
    q_.pop_front();
    --size_;
    buf2 = q_.front();

    return true;
}

void IQBufQueue::stop(void)
{
    done_ = true;
    cond_.notify_all();
}
