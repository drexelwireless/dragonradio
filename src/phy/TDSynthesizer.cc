#include "RadioConfig.hh"
#include "phy/PHY.hh"
#include "phy/TDSynthesizer.hh"
#include "phy/TXParams.hh"
#include "stats/Estimator.hh"

TDSynthesizer::TDSynthesizer(std::shared_ptr<PHY> phy,
                             double tx_rate,
                             const Channels &channels,
                             size_t nthreads)
  : Synthesizer(phy, tx_rate, channels)
  , done_(false)
  , mod_reconfigure_(nthreads)
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(true, std::memory_order_release);
        mod_threads_.emplace_back(std::thread(&TDSynthesizer::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i]),
                                              i));
    }
}

TDSynthesizer::~TDSynthesizer()
{
    stop();
}

void TDSynthesizer::modulate(const std::shared_ptr<Slot> &slot)
{
    std::atomic_store_explicit(&curslot_, slot, std::memory_order_release);
}

void TDSynthesizer::reconfigure(void)
{
    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_release);
}

void TDSynthesizer::stop(void)
{
    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

void TDSynthesizer::modWorker(std::atomic<bool> &reconfig, unsigned tid)
{
    Channels                           channels;
    Schedule                           schedule;
    double                             tx_rate;
    std::unique_ptr<ChannelState>      mod;
    std::shared_ptr<Synthesizer::Slot> prev_slot;
    std::shared_ptr<Synthesizer::Slot> slot;
    std::vector<size_t>                slot_chanidx; // TX channel for each slot
    size_t                             chanidx = 0;  // Index of TX channel
    std::shared_ptr<NetPacket>         pkt;

    while (!done_) {
        // Wait for the next slot
        do {
            slot = std::atomic_load_explicit(&curslot_, std::memory_order_acquire);
        } while (!done_ && slot == prev_slot);

        // Exit now if we're done
        if (done_)
            break;

        // Reconfigure if necessary
        if (reconfig.load(std::memory_order_acquire)) {
            // Make local copies to ensure thread safety
            channels = channels_;
            schedule = schedule_;
            tx_rate = tx_rate_;

            // If we have no schedule or channels, yield and try again
            if (schedule.size() == 0 ||
                channels.size() == 0 ||
                slot->slotidx >= schedule[0].size()) {
                std::this_thread::yield();
                continue;
            }

            // Cache which channel we use in each slot
            size_t nslots = schedule[0].size();

            slot_chanidx.resize(nslots);

            for (size_t slot = 0; slot < nslots; ++slot)
                schedule.firstChannelIdx(slot, slot_chanidx[slot]);

            // We need to update the modulator
            mod.release();

            reconfig.store(false, std::memory_order_relaxed);
        }

        if (!mod || slot_chanidx[slot->slotidx] != chanidx) {
            // Update our channel index
            chanidx = slot_chanidx[slot->slotidx];

            // Reconfigure the modulator
            mod = std::make_unique<ChannelState>(*phy_,
                                                 channels[chanidx].first,
                                                 channels[chanidx].second,
                                                 tx_rate);
        }

        // We can overfill if we are allowed to transmit on the same channel in
        // the next slot in the schedule
        const Schedule::slot_type &slots = schedule[chanidx];

        // Determine maximum number of samples in this slot
        bool overfill = getSuperslots() && slots[(chanidx + 1) % slots.size()];

        if (overfill) {
            std::lock_guard<spinlock_mutex> lock(slot->mutex);

            slot->max_samples = slot->max_superslot_samples;
        }

        // Modulate packets for the current slot
        while (!done_) {
            // Get a packet to modulate
            if (!pkt) {
                if (!sink.pull(pkt))
                    continue;
            }

            // If the slot is closed, bail.
            if (slot->closed.load(std::memory_order_relaxed))
                break;

            // If this is a timestamped packet, timestamp it. In any case,
            // modulate it.
            std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
            bool                       pushed;

            /* If the packet requires a timestamp, we must acquire the slot's
             * mutex before modulation to ensure slot->nsamples doesn't change
             * out from under us.
             */
            if (pkt->isInternalFlagSet(kIsTimestamp)) {
                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                pkt->appendTimestamp(Clock::to_mono_time(slot->deadline) + (slot->delay + slot->nsamples)/phy_->getTXRate());

                mod->modulate(std::move(pkt), *mpkt);

                pushed = slot->push(mpkt, overfill);
            } else {
                mod->modulate(std::move(pkt), *mpkt);

                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                pushed = slot->push(mpkt, overfill);
            }

            if (!pushed) {
                pkt = std::move(mpkt->pkt);

                if (pkt->isInternalFlagSet(kIsTimestamp))
                    pkt->removeTimestamp();
            }
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

TDSynthesizer::ChannelState::ChannelState(PHY &phy,
                                          const Channel &channel,
                                          const std::vector<C> &taps,
                                          double tx_rate)
  : channel_(channel)
  // XXX Protected against channel with zero bandwidth
  , rate_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw))
  , rad_(2*M_PI*channel.fc/tx_rate)
  , resamp_(rate_, taps)
  , mod_(phy.mkModulator())
{
    resamp_.setFreqShift(rad_);
}

void TDSynthesizer::ChannelState::reset(void)
{
    resamp_.reset();
}

void TDSynthesizer::ChannelState::modulate(std::shared_ptr<NetPacket> pkt,
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
    mpkt.channel = channel_;
}
