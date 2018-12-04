#include "RadioConfig.hh"
#include "dsp/NCO.hh"
#include "phy/PHY.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/TXParams.hh"
#include "net/Net.hh"
#include "stats/Estimator.hh"

ParallelPacketModulator::ParallelPacketModulator(std::shared_ptr<Net> net,
                                                 std::shared_ptr<PHY> phy,
                                                 const Channels &channels,
                                                 size_t nthreads)
  : PacketModulator(channels)
  , sink(*this, nullptr, nullptr)
  , net_(net)
  , phy_(phy)
  , done_(false)
  , nwanted_(0)
  , nsamples_(0)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ParallelPacketModulator::modWorker, this));
}

ParallelPacketModulator::~ParallelPacketModulator()
{
    stop();
}

void ParallelPacketModulator::modulate(size_t n)
{
    std::unique_lock<std::mutex> lock(pkt_mutex_);

    if (n > nsamples_) {
        nwanted_ = n - nsamples_;
        producer_cond_.notify_all();
    }
}

size_t ParallelPacketModulator::pop(std::list<std::unique_ptr<ModPacket>>& pkts,
                                    size_t maxSamples,
                                    bool overfill)
{
    size_t nsamples = 0;

    {
        std::unique_lock<std::mutex> lock(pkt_mutex_);

        while (!pkt_q_.empty()) {
            ModPacket& mpkt = *(pkt_q_.front());

            if (mpkt.complete.test_and_set(std::memory_order_acquire))
                break;

            size_t n = mpkt.samples->size();

            // Drop packets that won't fit in a slot
            if (n > maxPacketSize_ && maxPacketSize_ != 0) {
                fprintf(stderr, "Dropping modulated packet that is too long to send: n=%u, max=%u\n",
                        (unsigned) n,
                        (unsigned) maxSamples);
                pkt_q_.pop();
                nsamples_ -= n;
                continue;
            }

            // If we don't have enough room to pop this packet and we're not
            // overfilling, break out of the loop.
            if (n > maxSamples && !overfill) {
                mpkt.complete.clear(std::memory_order_release);
                break;
            }

            // Pop the packet
            pkts.emplace_back(std::move(pkt_q_.front()));
            pkt_q_.pop();
            nsamples_ -= n;
            nsamples += n;

            // If we just overfilled, break out of the loop. We can't subtract n
            // from maxSamples here because n > maxSamples and maxSamples is
            // unsigned!
            if (n > maxSamples)
                break;

            // Update maximum number of samples that remain to pop
            maxSamples -= n;
        }
    }

    producer_cond_.notify_all();

    return nsamples;
}

void ParallelPacketModulator::reconfigure(void)
{
}

void ParallelPacketModulator::stop(void)
{
    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;
    producer_cond_.notify_all();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

void ParallelPacketModulator::modWorker(void)
{
    auto                       modulator = phy_->mkModulator();
    std::shared_ptr<NetPacket> pkt;
    ModPacket                  *mpkt;
    // We want the last 10 packets to account for 86% of the EMA
    EMA<double>                samples_per_packet(2.0/(10.0 + 1.0));

    for (;;) {
        size_t estimated_samples = samples_per_packet.getValue();

        // Wait for there to be room for us to add another packet
        {
            std::unique_lock<std::mutex> lock(pkt_mutex_);

            producer_cond_.wait(lock, [this, estimated_samples]
                {
                    if (done_)
                        return true;

                    if (nwanted_ >= estimated_samples) {
                        nwanted_ -= estimated_samples;
                        return true;
                    }

                    return false;
                });
        }

        // Exit if we are done
        if (done_)
            break;

        {
            // Get a packet from the network
            std::unique_lock<std::mutex> net_lock(net_mutex_);

            if (!sink.pull(pkt))
                continue;

            // Now place a ModPacket in our queue. The packet isn't complete
            // yet, but we need to put it in the queue now to ensure that
            // packets are modulated in the order they are received from the
            // network. Note that we acquire the lock on the network first, then
            // the lock on the queue. We don't want to hold the lock on the
            // queue for long because that will starve the transmitter.
            std::unique_lock<std::mutex> lock(pkt_mutex_);

            pkt_q_.emplace(std::make_unique<ModPacket>());
            mpkt = pkt_q_.back().get();
        }

        // Modulate the packet
        modulator->modulate(std::move(pkt), getTXShift(), *mpkt);

        // Save the number of modulated samples so we can record them later.
        size_t n = mpkt->samples->size();

        // Update estimate of samples-per-packet
        samples_per_packet.update(n);

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
            std::lock_guard<std::mutex> lock(pkt_mutex_);

            nsamples_ += n;

            // If we underproduced, kick off another producer
            if (estimated_samples > n) {
                nwanted_ += estimated_samples - n;
                producer_cond_.notify_one();
            }
        }
    }
}
