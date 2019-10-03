#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "RadioConfig.hh"
#include "Util.hh"
#include "phy/FDSynthesizer.hh"
#include "phy/PHY.hh"
#include "phy/TXParams.hh"
#include "stats/Estimator.hh"

FDSynthesizer::FDSynthesizer(std::shared_ptr<PHY> phy,
                             double tx_rate,
                             const Channels &channels,
                             size_t nthreads)
  : Synthesizer(phy, tx_rate, channels)
  , done_(false)
  , mod_reconfigure_(nthreads)
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(true, std::memory_order_release);
        mod_threads_.emplace_back(std::thread(&FDSynthesizer::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i]),
                                              i));
    }
}

FDSynthesizer::~FDSynthesizer()
{
    stop();
}

void FDSynthesizer::modulate(const std::shared_ptr<Slot> &slot)
{
    std::atomic_store_explicit(&curslot_, slot, std::memory_order_release);
}

void FDSynthesizer::reconfigure(void)
{
    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_release);
}

void FDSynthesizer::stop(void)
{
    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

void FDSynthesizer::modWorker(std::atomic<bool> &reconfig, unsigned tid)
{
    Channels                           channels;
    Schedule                           schedule;
    double                             tx_rate = tx_rate_;
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
            std::lock_guard<spinlock_mutex> lock(mutex_);

            // Make local copies to ensure thread safety
            channels = channels_;
            schedule = schedule_;
            tx_rate = tx_rate_;

            // If we have no schedule or channels, yield and try again
            if (schedule.size() == 0 || channels.size() == 0) {
                reconfig.store(false, std::memory_order_relaxed);
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

        // Skip illegal slot indices
        if (slot->slotidx >= slot_chanidx.size()) {
            logEvent("PHY: Bad slot index");
            continue;
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
        bool overfill = getSuperslots() && slots[(slot->slotidx + 1) % slots.size()];

        if (overfill) {
            std::lock_guard<spinlock_mutex> lock(slot->mutex);

            slot->max_samples = slot->full_slot_samples;
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
            if (pkt->internal_flags.is_timestamp) {
                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                pkt->appendTimestamp(Clock::to_mono_time(slot->deadline) + (slot->deadline_delay + slot->nsamples)/tx_rate_);

                mod->modulate(std::move(pkt), *mpkt);

                pushed = slot->push(mpkt, chanidx, overfill);
            } else {
                mod->modulate(std::move(pkt), *mpkt);

                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                pushed = slot->push(mpkt, chanidx, overfill);
            }

            if (!pushed) {
                pkt = std::move(mpkt->pkt);

                if (pkt->internal_flags.is_timestamp)
                    pkt->removeTimestamp();
            }
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

FDSynthesizer::ChannelState::ChannelState(PHY &phy,
                                          const Channel &channel,
                                          const std::vector<C> &taps,
                                          double tx_rate)
  : Upsampler(phy.getMinTXRateOversample(), tx_rate/channel.bw, N*(channel.fc/tx_rate))
  , channel_(channel)
  // XXX Protected against channel with zero bandwidth
  , rate_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw))
  , mod_(phy.mkModulator())
{
}

void FDSynthesizer::ChannelState::modulate(std::shared_ptr<NetPacket> pkt,
                                           ModPacket &mpkt)
{
    const unsigned Li = X*L/I; // Number of samples consumed per input block
    float          g = pkt->g;

    // Interpolate if needed
    if (Nrot != 0 || rate_ != 1.0) {
        // Modulate the packet, but don't paply gain yet. We will apply gain
        // when we resample.
        mod_->modulate(std::move(pkt), 1.0f, mpkt);

        // Perform overlap-save on modulated signal to upsample it.
        //
        // Each block of Li input samples results in a block of N output
        // frequency domain samples. We add Li - 1 to round up.
        //
        // We zero the frequency-domain buffer because we only copy our signal
        // into the frequency bins it occupies in the upsampled frequency space
        // while leaving the other binds untouched.
        auto   iqbuf = std::move(mpkt.samples);
        auto   fdbuf = std::make_shared<IQBuf>(N*((iqbuf->size() + Li - 1)/Li));
        size_t nsamples = 0;
        size_t fdnsamples = 0;

        fdbuf->zero();
        reset();
        upsample(iqbuf->data(),
                 iqbuf->size(),
                 fdbuf->data(),
                 g,
                 true,
                 nsamples,
                 I*iqbuf->size()/X,
                 fdnsamples);
        fdbuf->resize(fdnsamples);

        // Now convert upsampled signal back to time domain
        auto iqbuf_up = std::make_shared<IQBuf>(L*((fdbuf->size() + N - 1)/N));

        timedomain_.toTimeDomain(fdbuf->data(), fdbuf->size(), iqbuf_up->data());

        iqbuf_up->resize(I*iqbuf->size()/X);

        // Put samples back into ModPacket
        mpkt.offset = 0;
        mpkt.nsamples = iqbuf_up->size();
        mpkt.samples = std::move(iqbuf_up);
    } else
        // Modulate packet and apply gain
        mod_->modulate(std::move(pkt), g, mpkt);

    // Set channel
    mpkt.channel = channel_;
}
