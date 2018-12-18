#include "RadioConfig.hh"
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
  , upsamp_params(std::bind(&PacketModulator::reconfigure, this))
  , net_(net)
  , phy_(phy)
  , done_(false)
  , mod_reconfigure_(nthreads)
  , nwanted_(0)
  , nsamples_(0)
  , one_mod_(phy->mkModulator())
  , one_modparams_(upsamp_params,
                   phy_->getTXRate(),
                   phy_->getTXUpsampleRate(),
                   getTXShift())
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(false, std::memory_order_relaxed);
        mod_threads_.emplace_back(std::thread(&ParallelPacketModulator::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i])));
    }
}

ParallelPacketModulator::~ParallelPacketModulator()
{
    stop();
}

void ParallelPacketModulator::modulateOne(std::shared_ptr<NetPacket> pkt,
                                          ModPacket &mpkt)
{
    modulateWithParams(*one_mod_, one_modparams_, std::move(pkt), mpkt);
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
        auto                         it = pkt_q_.begin();

        while (it != pkt_q_.end()) {
            ModPacket& mpkt = **it;

            // If modulation is incomplete, try the next packet
            if (mpkt.incomplete.test_and_set(std::memory_order_acquire)) {
                ++it;
                continue;
            }

            // Save the size of the packet so we can update counters later
            size_t n = mpkt.samples->size();

            // Drop packets that won't fit in a slot
            if (n > maxPacketSize_ && maxPacketSize_ != 0) {
                fprintf(stderr, "Dropping modulated packet that is too long to send: n=%u, max=%u\n",
                        (unsigned) n,
                        (unsigned) maxSamples);

                pkt_q_.erase(it++);
                nsamples_ -= n;
                continue;
            }

            // If we don't have enough room to pop this packet and we're not
            // overfilling, break out of the loop.
            if (n > maxSamples && !overfill) {
                mpkt.incomplete.clear(std::memory_order_release);
                break;
            }

            // Pop the packet
            pkts.emplace_back(std::move(*it));
            pkt_q_.erase(it++);
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
    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_relaxed);
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

void ParallelPacketModulator::modulateWithParams(PHY::Modulator &modulator,
                                                 ModParams &params,
                                                 std::shared_ptr<NetPacket> pkt,
                                                 ModPacket &mpkt)
{
    // Modulate the packet
    modulator.modulate(std::move(pkt), mpkt);

    // Frequency shift and upsample
    if (params.shift != 0.0 || params.resamp_rate != 1.0) {
        // Get samples from ModPacket
        auto iqbuf = std::move(mpkt.samples);

        // Up-sample
        iqbuf->append(ceil(params.resamp.getDelay()));

        auto iqbuf_up = params.resamp.resample(*iqbuf);

        iqbuf_up->delay = floor(params.resamp_rate*params.resamp.getDelay());

        iqbuf = iqbuf_up;

        // Mix up
        params.nco.mix_up(iqbuf->data(), iqbuf->data(), iqbuf->size());

        // Put samples back into ModPacket
        mpkt.samples = std::move(iqbuf);
    }

    // Set center frequency
    mpkt.fc = params.shift;
}

void ParallelPacketModulator::modWorker(std::atomic<bool> &reconfig)
{
    auto                        modulator = phy_->mkModulator();
    std::shared_ptr<NetPacket>  pkt;
    ModPacket                   *mpkt;
    ModParams                   modparams(upsamp_params,
                                          phy_->getTXRate(),
                                          phy_->getTXUpsampleRate(),
                                          getTXShift());
    // We want the last 10 packets to account for 86% of the EMA
    EMA<double>                 samples_per_packet(2.0/(10.0 + 1.0));

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
            //
            // Although we modulate packets in order, we have now relaxed the
            // restriction that they be *sent* in order (see
            // ParallelPacketModulator::pop).
            std::unique_lock<std::mutex> lock(pkt_mutex_);

            // Packets containing a selective ACK are prioritized over other
            // packets.
            if (pkt->isInternalFlagSet(kHasSelectiveACK)) {
                pkt_q_.emplace_front(std::make_unique<ModPacket>());
                mpkt = pkt_q_.front().get();
            } else {
                pkt_q_.emplace_back(std::make_unique<ModPacket>());
                mpkt = pkt_q_.back().get();
            }
        }

        // Reconfigure if necessary
        if (reconfig.load(std::memory_order_relaxed)) {
            modparams.reconfigure(phy_->getTXRate(),
                                  phy_->getTXUpsampleRate(),
                                  getTXShift());

            reconfig.store(false, std::memory_order_relaxed);
        }

        // Modulate the packet
        modulateWithParams(*modulator, modparams, std::move(pkt), *mpkt);

        // Save the number of modulated samples so we can record them later.
        size_t n = mpkt->samples->size();

        // Update estimate of samples-per-packet
        samples_per_packet.update(n);

        // Mark the modulated packet as complete. The packet may be invalidated
        // by a consumer immediately after we mark it complete, so we cannot use
        // the mpkt pointer after this statement!
        mpkt->incomplete.clear(std::memory_order_release);

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
