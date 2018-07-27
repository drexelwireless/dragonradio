#include "RadioConfig.hh"
#include "WorkQueue.hh"
#include "phy/PHY.hh"
#include "phy/ParallelPacketModulator.hh"
#include "phy/TXParams.hh"
#include "net/Net.hh"

ParallelPacketModulator::ParallelPacketModulator(std::shared_ptr<Net> net,
                                                 std::shared_ptr<PHY> phy,
                                                 size_t nthreads)
  : sink(*this, nullptr, nullptr)
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

void ParallelPacketModulator::modulate(size_t n)
{
    std::unique_lock<std::mutex> lock(pkt_mutex_);

    if (n > nsamples_) {
        nwanted_ = n - nsamples_;
        producer_cond_.notify_all();
    }
}

void ParallelPacketModulator::pop(std::list<std::unique_ptr<ModPacket>>& pkts, size_t maxSamples)
{
    {
        std::unique_lock<std::mutex> lock(pkt_mutex_);

        while (!pkt_q_.empty()) {
            ModPacket& mpkt = *(pkt_q_.front());

            if (mpkt.complete.test_and_set(std::memory_order_acquire))
                break;

            size_t n = mpkt.samples->size();

            if (n > maxPacketSize_ && maxPacketSize_ != 0) {
                fprintf(stderr, "Dropping modulated packet that is too long to send: n=%u, max=%u\n",
                        (unsigned) n,
                        (unsigned) maxSamples);
                pkt_q_.pop();
                nsamples_ -= n;
                continue;
            }

            if (n > maxSamples) {
                mpkt.complete.clear(std::memory_order_release);
                break;
            }

            pkts.emplace_back(std::move(pkt_q_.front()));
            pkt_q_.pop();
            nsamples_ -= n;
            maxSamples -= n;
        }
    }

    producer_cond_.notify_all();
}

void ParallelPacketModulator::modWorker(void)
{
    std::unique_ptr<PHY::Modulator> modulator = phy_->make_modulator();
    std::shared_ptr<NetPacket>      pkt;
    ModPacket                       *mpkt;

    for (;;) {
        // Wait for queue to be below low-water mark
        {
            std::unique_lock<std::mutex> lock(pkt_mutex_);

            producer_cond_.wait(lock, [this]{ return done_ || nwanted_ > 0; });
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

        // Save packet's TXParams and gain multiplier
        TXParams *tx_params = pkt->tx_params;
        float g = pkt->g;

        // Modulate the packet
        modulator->modulate(*mpkt, std::move(pkt));

        // Save the number of modulated samples so we can record them later.
        size_t n = mpkt->samples->size();

        // Pass the modulated packet to the 0dBFS estimator if requested
        if (tx_params->nestimates_0dBFS > 0) {
            --tx_params->nestimates_0dBFS;
            work_queue.submit(&TXParams::autoSoftGain0dBFS, tx_params, g, mpkt->samples);
        }

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
            nwanted_ -= n;
        }
    }
}
