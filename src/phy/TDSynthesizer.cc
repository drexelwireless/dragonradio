#include "RadioConfig.hh"
#include "phy/PHY.hh"
#include "phy/TDSynthesizer.hh"
#include "phy/TXParams.hh"
#include "net/Net.hh"
#include "stats/Estimator.hh"

TDSynthesizer::TDSynthesizer(std::shared_ptr<Net> net,
                             std::shared_ptr<PHY> phy,
                             double tx_rate,
                             const Channel &tx_channel,
                             size_t nthreads)
  : Synthesizer(phy, tx_rate)
  , net_(net)
  , done_(false)
  , taps_({1.0})
  , tx_channel_(tx_channel)
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

double TDSynthesizer::getMaxTXUpsampleRate(void)
{
    if (tx_channel_.bw == 0.0)
        return 1.0;
    else
        return tx_rate_/(phy_->getMinRXRateOversample()*tx_channel_.bw);
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
    std::unique_ptr<ChannelState>      mod;
    std::shared_ptr<Synthesizer::Slot> prev_slot;
    std::shared_ptr<Synthesizer::Slot> slot;
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
            mod = std::make_unique<ChannelState>(*phy_, tx_channel_, taps_, tx_rate_);

            reconfig.store(false, std::memory_order_relaxed);
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

                pushed = slot->push(mpkt, slot->overfill);
            } else {
                mod->modulate(std::move(pkt), *mpkt);

                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                pushed = slot->push(mpkt, slot->overfill);
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
