#include "RadioConfig.hh"
#include "phy/PHY.hh"
#include "phy/TDSynthesizer.hh"
#include "phy/TXParams.hh"
#include "net/Net.hh"
#include "stats/Estimator.hh"

TDSynthesizer::TDSynthesizer(std::shared_ptr<Net> net,
                             std::shared_ptr<PHY> phy,
                             const Channel &tx_channel,
                             size_t nthreads)
  : Synthesizer()
  , sink(*this, nullptr, nullptr)
  , net_(net)
  , phy_(phy)
  , done_(false)
  , taps_({1.0})
  , tx_channel_(tx_channel)
  , mod_reconfigure_(nthreads)
  , nwanted_(0)
  , nsamples_(0)
  , one_mod_(*phy_, taps_, 1.0, 0.0)
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(false, std::memory_order_relaxed);
        mod_threads_.emplace_back(std::thread(&TDSynthesizer::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i])));
    }
}

TDSynthesizer::~TDSynthesizer()
{
    stop();
}

double TDSynthesizer::getMaxTXUpsampleRate(void)
{
    return getTXUpsampleRate();
}

void TDSynthesizer::modulateOne(std::shared_ptr<NetPacket> pkt,
                                          ModPacket &mpkt)
{
    one_mod_.modulate(tx_channel_, std::move(pkt), mpkt);
}

void TDSynthesizer::modulate(size_t n)
{
    std::unique_lock<std::mutex> lock(pkt_mutex_);

    if (n > nsamples_) {
        nwanted_ = n - nsamples_;
        producer_cond_.notify_all();
    }
}

size_t TDSynthesizer::pop(std::list<std::unique_ptr<ModPacket>>& pkts,
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
            if (n > max_packet_size_ && max_packet_size_ != 0) {
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

void TDSynthesizer::reconfigure(void)
{
    one_mod_.setTaps(taps_);
    one_mod_.setRate(getTXUpsampleRate());
    one_mod_.setFreqShift(2*M_PI*tx_channel_.fc/tx_rate_);

    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_relaxed);
}

void TDSynthesizer::stop(void)
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

void TDSynthesizer::modWorker(std::atomic<bool> &reconfig)
{
    ChannelState               mod(*phy_, taps_, 1.0, 0.0);
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
            //
            // Although we modulate packets in order, we have now relaxed the
            // restriction that they be *sent* in order (see
            // TDSynthesizer::pop).
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
            mod.setTaps(taps_);
            mod.setRate(getTXUpsampleRate());
            mod.setFreqShift(2*M_PI*tx_channel_.fc/tx_rate_);

            reconfig.store(false, std::memory_order_relaxed);
        }

        // Modulate the packet
        mod.modulate(tx_channel_, std::move(pkt), *mpkt);

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

void TDSynthesizer::ChannelState::modulate(const Channel &channel,
                                           std::shared_ptr<NetPacket> pkt,
                                           ModPacket &mpkt)
{
    // Modulate the packet
    mod_->modulate(std::move(pkt), mpkt);

    // Upsample if needed
    if (rad_ != 0.0 || rate_ != 1.0) {
        // Get samples from ModPacket
        auto iqbuf = std::move(mpkt.samples);

        // Append zeroes to compensate for delay
        iqbuf->append(ceil(resamp_.getDelay()));

        // Resample and mix up
        auto     iqbuf_up = std::make_shared<IQBuf>(resamp_.neededOut(iqbuf->size()));
        unsigned nw;

        nw = resamp_.resampleMixUp(iqbuf->data(), iqbuf->size(), iqbuf_up->data());
        assert(nw <= iqbuf_up->size());
        iqbuf_up->resize(nw);

        // Indicate delay
        iqbuf_up->delay = floor(resamp_.getRate()*resamp_.getDelay());

        // Put samples back into ModPacket
        mpkt.samples = std::move(iqbuf_up);
    }

    // Set channel
    mpkt.channel = channel;
}
