#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "RadioConfig.hh"
#include "Util.hh"
#include "phy/MultichannelSynthesizer.hh"
#include "phy/PHY.hh"
#include "stats/Estimator.hh"

MultichannelSynthesizer::MultichannelSynthesizer(std::shared_ptr<PHY> phy,
                                                 double tx_rate,
                                                 const Channels &channels,
                                                 size_t nthreads)
  : Synthesizer(phy, tx_rate, channels)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&MultichannelSynthesizer::modWorker,
                                              this,
                                              i));

    reconfigure();
}

MultichannelSynthesizer::~MultichannelSynthesizer()
{
    stop();
}

void MultichannelSynthesizer::modulate(const std::shared_ptr<Slot> &slot)
{
    std::atomic_store_explicit(&curslot_, slot, std::memory_order_release);
}

void MultichannelSynthesizer::finalize(Slot &slot)
{
    if (!slot.fdbuf)
        return;

    // If we've already converted the frequency-domain buffer to a time-domain
    // buffer, there's nothing left to do.
    if (!slot.iqbufs.empty())
        return;

    // Flush all synthesis state
    size_t nchannels = mods_.size();

    for (unsigned channelidx = 0; channelidx < nchannels; ++channelidx) {
        const Schedule::slot_type &slots = schedule_copy_[channelidx];

        // Skip this channel if we're not allowed to modulate
        if (slots[slot.slotidx])
            mods_[channelidx]->flush(slot);
    }

    // If we have any samples, our delay will always be less than nsamples, so
    // we can just check that nsamples is non-zero here.
    if (slot.nsamples == 0)
        return;

    // Convert the frequency-domain signal back to the time domain
    auto iqbuf = std::make_shared<IQBuf>(L*(slot.fdnsamples/N));

    assert(slot.fdnsamples <= slot.fdbuf->size());
    assert(slot.fdnsamples % N == 0);

    timedomain_.toTimeDomain(slot.fdbuf->data(), slot.fdnsamples, iqbuf->data());
    iqbuf->delay = slot.delay;
    iqbuf->resize(slot.nsamples);

    slot.iqbufs.emplace_back(std::move(iqbuf));
}

void MultichannelSynthesizer::reconfigure(void)
{
    std::lock_guard<spinlock_mutex> lock(mods_mutex_);

    // Tell workers we are reconfiguring
    reconfigure_.store(true, std::memory_order_release);

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }

    // Wait for workers to be ready for reconfiguration
    reconfigure_sync_.wait();

    // Make vopies of variables for thraad safety
    tx_rate_copy_ = tx_rate_;
    channels_copy_ = channels_;
    schedule_copy_ = schedule_;

    // Count number of channels we may transmit on in each slot
    if (schedule_.size() > 0) {
        channel_count_.resize(schedule_[0].size());
        std::fill(channel_count_.begin(), channel_count_.end(), 0);

        for (unsigned chanidx = 0; chanidx < schedule_.size(); ++chanidx) {
            auto &slots = schedule_[chanidx];

            for (unsigned slotidx = 0; slotidx < slots.size(); ++slotidx) {
                if (slots[slotidx])
                    ++channel_count_[slotidx];
            }
        }
    }

    // Now set the channels and reconfigure the channel state
    const unsigned nchannels = channels_copy_.size();

    mods_.resize(nchannels);

    for (unsigned i = 0; i < nchannels; i++)
        mods_[i] = std::make_unique<ChannelState>(*phy_,
                                                  channels_copy_[i].first,
                                                  channels_copy_[i].second,
                                                  tx_rate_copy_);

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();
}

void MultichannelSynthesizer::stop(void)
{
    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;

    wake_cond_.notify_all();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

void MultichannelSynthesizer::modWorker(unsigned tid)
{
    std::shared_ptr<Synthesizer::Slot> prev_slot;
    std::shared_ptr<Synthesizer::Slot> slot;
    std::unique_ptr<ModPacket>         mpkt;
    std::shared_ptr<NetPacket>         pkt;

    while (!done_) {
        // Wait for the next slot if we are starting at the first channel for
        // which we are responsible.
        do {
            slot = std::atomic_load_explicit(&curslot_, std::memory_order_acquire);
        } while (!done_ && !reconfigure_.load(std::memory_order_acquire) && slot == prev_slot);

        // Exit now if we're done
        if (done_)
            break;

        // If we are reconfiguring, wait until reconfiguration is done
        if (reconfigure_.load(std::memory_order_acquire)) {
            // Wait for reconfiguration to start
            reconfigure_sync_.wait();

            // Wait for reconfiguration to finish
            reconfigure_sync_.wait();

            // If we are unneeded, sleep
            if (tid >= channels_copy_.size()) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return done_ || reconfigure_.load(std::memory_order_acquire); });

                continue;
            }

            // Otherwise, get the next slot
            continue;
        }

        // If we don't have a schedule yet, try again
        if (schedule_copy_.size() == 0 || slot->slotidx > schedule_copy_[0].size()) {
            std::this_thread::yield();
            continue;
        }

        // Get the frequency-domain buffer for the slot, creating it if it does
        // not yet exist
        {
            std::lock_guard<spinlock_mutex> lock(slot->mutex);

            // We allocate (and zero) a frequency-domain buffer for everyone to
            // use if we are the first to get access to this slot.
            if (!slot->fdbuf) {
                // Each block of L input samples results in a block of N output
                // frequency domain samples. We add L-1 to round up to the next
                // partial block and one extra block to account for possible
                // overflow from the previous slot.
                slot->fdbuf = std::make_unique<IQBuf>(N*(1 + (slot->full_slot_samples + L - 1)/L));
                slot->fdbuf->zero();
            }
        }

        for (unsigned channelidx = tid; channelidx < channels_copy_.size(); channelidx += nthreads_) {
            // Get channel state for current channel
            ChannelState              &mod = *mods_[channelidx];
            const Schedule::slot_type &slots = schedule_copy_[channelidx];

            // Skip this channel if we're not allowed to modulate
            if (!slots[slot->slotidx])
                continue;

            // Determine gain factor to compensate for number of channels we can
            // transmit on. We are conservative: if we can transmit on N
            // channels in this slot, then we apply a multiplicative gain of 1/N
            // to all packets whose transmission begins in this slot.
            float g_nchan = 1.0f/static_cast<float>(channel_count_[slot->slotidx]);

            // We can overfill if we are allowed to transmit on the same channel
            // in the next slot in the schedule
            bool overfill = getSuperslots() && slots[(slot->slotidx + 1) % slots.size()];

            {
                std::lock_guard<spinlock_mutex> lock(slot->mutex);

                if (overfill)
                    slot->max_samples = slot->full_slot_samples;
            }

            // Modulate into a new slot
            mod.nextSlot(prev_slot.get(), *slot, overfill);

            // Do upsampling of leftover IQ buffer here
            if (mod.iqbuf) {
                mod.iqbufoff += mod.upsample();

                // This should never happen!
                if (mod.iqbufoff != mod.iqbuf->size())
                    logEvent("PHY: leftover IQ buffer bigger than slot!");

                mod.iqbuf.reset();
                assert(mod.pkt);
                mod.pkt.reset();
            }

            // Modulate packets for the current slot
            while (!done_) {
                // If we don't have a modulated packet already, get a packet to
                // modulate (if needed), and then create a ModPacket for modulation.
                if (!mpkt) {
                    if (!pkt) {
                        if (!sink.pull(pkt))
                            continue;
                    }

                    mpkt = std::make_unique<ModPacket>();
                }

                // If the slot is closed, bail.
                if (slot->closed.load(std::memory_order_relaxed))
                    break;

                // If this is a timestamped packet, timestamp it. In any case,
                // modulate it.
                if (!mpkt->pkt) {
                    MonoClock::time_point timestamp = Clock::to_mono_time(slot->deadline) + (slot->deadline_delay + mod.nsamples - mod.delay)/tx_rate_copy_;

                    pkt->timestamp = timestamp;

                    if (pkt->internal_flags.is_timestamp)
                        pkt->appendTimestamp(timestamp);

                    float g = phy_->mcs_table[pkt->mcsidx].autogain.getSoftTXGain()*g_nchan;

                    mod.modulate(std::move(pkt), g, *mpkt);
                }

                // Determine whether or not we can fit this modulated packet.
                bool pushed = false;

                if (mod.fits(*mpkt, overfill)) {
                    // We must upsample the modulated packet's IQ buffer
                    mod.setIQBuffer(std::move(mpkt->samples));

                    // Do upsampling here. Note that we may not be able to fit the
                    // entire upsampled data into the current slot.
                    size_t nsamples0 = mod.nsamples;
                    size_t n = mod.upsample();

                    {
                        std::lock_guard<spinlock_mutex> lock(slot->mutex);

                        if (!slot->closed.load(std::memory_order_acquire)) {
                            // Set modulated packet's start and number of
                            // samples with respect to final time-domain IQ
                            // buffer.
                            mpkt->offset = nsamples0;
                            mpkt->nsamples = mod.upsampledSize(mod.iqbuf->size() - mod.iqbuf->delay);

                            // If we pushed the packet, record the new offset
                            // into the IQ buffer.
                            mod.iqbufoff += n;

                            // If the packet did fit entirely within the slot,
                            // save a copy of the un-modulated packet so if
                            // there is an error and we can't transmit the rest
                            // of the packet in the next slot, then we can
                            // re-modulate it.
                            if (mod.iqbufoff != mod.iqbuf->size())
                                mod.pkt = mpkt->pkt;

                            slot->mpkts.emplace_back(std::move(mpkt));

                            pushed = true;
                        }
                    }

                    // If we pushed the packet and it fit entirely in the slot, free
                    // the buffer; otherwise, keep the buffer around so we can put
                    // the rest of it into the next slot.
                    //
                    // If we didn't push the packet, put the samples back into the
                    // modulated packet.
                    if (pushed) {
                        if (mod.iqbufoff == mod.iqbuf->size())
                            mod.iqbuf.reset();
                        else
                            break;
                    } else {
                        logEvent("PHY: failed to add packet to slot: seq=%u",
                            (unsigned) mpkt->pkt->hdr.seq);
                        mpkt->samples = std::move(mod.iqbuf);
                    }
                }

                // If we couldn't add the packet to the slot and it is a timestamped
                // packet, we need to strip it of its (now-inaccurate) timestamp and
                // re-modulate it when we get a slot for it.
                if (!pushed) {
                    if (mpkt->pkt->internal_flags.is_timestamp) {
                        pkt = std::move(mpkt->pkt);
                        pkt->removeTimestamp();
                        mpkt.reset();
                    }
                    break;
                }
            }
        }

        // We are done with this slot. Finalize it if everyone else has finished
        // too.
        if (slot->nfinished.fetch_add(std::memory_order_relaxed) == nthreads_ - 1) {
            std::lock_guard<spinlock_mutex> lock(slot->mutex);

            if (!slot->closed.load(std::memory_order_acquire))
                finalize(*slot);
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

MultichannelSynthesizer::ChannelState::ChannelState(PHY &phy,
                                                    const Channel &channel,
                                                    const std::vector<C> &taps,
                                                    double tx_rate)
  : Upsampler(phy.getMinTXRateOversample(), tx_rate/channel.bw, N*(channel.fc/tx_rate))
  , fdbuf(nullptr)
  , delay(0)
  , nsamples(0)
  , max_samples(0)
  , npartial(0)
  , fdnsamples(0)
  , channel_(channel)
  // XXX Protected against channel with zero bandwidth
  , rate_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw))
  , mod_(phy.mkPacketModulator())
{
}

void MultichannelSynthesizer::ChannelState::nextSlot(const Slot *prev_slot,
                                                     Slot &slot,
                                                     const bool overfill)
{
    // It's safe to keep a plain old pointer since we will only keep this
    // pointer around as long as we have a reference to the slot, and the slot
    // owns this buffer.
    fdbuf = slot.fdbuf.get();

    max_samples = overfill ? slot.full_slot_samples : slot.max_samples;

    // Was a partial block output in the previous slot?
    if (prev_slot && prev_slot->npartial != 0) {
        if (npartial != 0) {
            // Any channel that outputs a partial block will have the same
            // number of partial samples.
            assert(npartial == prev_slot->npartial);

            // Copy the previously output FFT block
            upsampleBlock(fdbuf->data());

            // We start with a full FFT block of samples
            nsamples = L;
            fdnsamples = N;
        } else {
            // We didn't output a partial block, but somebody else did. Leave
            // the first frequency-domain output block empty---we didn't output
            // any signal yet.

            // Set up the FFT buffer to reflect the fact that we didn't output
            // any signal for the first npartial samples.
            reset(X*prev_slot->npartial/I);

            nsamples = 0;
            fdnsamples = 0;
        }

        delay = prev_slot->npartial;
        npartial = 0;
    } else {
        // If we are NOT continuing modulation of a slot, re-initialize the FFT
        // buffer. When a packet ends exactly on a slot boundar, npartial will
        // be 0, but we DO NOT want to re-initialize the upsampler. We test for
        // this case by seeing if the number of samples output in the previous
        // slot is equal to the size of the slot.
        if (prev_slot && nsamples != delay + prev_slot->full_slot_samples)
            reset();

        nsamples = 0;
        fdnsamples = 0;
        delay = 0;
        npartial = 0;
    }
}

void MultichannelSynthesizer::ChannelState::modulate(std::shared_ptr<NetPacket> pkt,
                                                     const float g,
                                                     ModPacket &mpkt)
{
    const float g_effective = pkt->g*g;

    // Modulate the packet
    mod_->modulate(std::move(pkt), g_effective, mpkt);

    // Set channel
    mpkt.channel = channel_;
}

bool MultichannelSynthesizer::ChannelState::fits(ModPacket &mpkt, const bool overfill)
{
    // This is the number of samples the upsampled signal will need
    size_t n = I*(mpkt.samples->size() - mpkt.samples->delay)/X;

    if (nsamples + npending() + n <= delay + max_samples ||
        (nsamples < delay + max_samples && overfill)) {
        mpkt.start = nsamples;
        mpkt.nsamples = n;

        return true;
    } else
        return false;
}

void MultichannelSynthesizer::ChannelState::setIQBuffer(std::shared_ptr<IQBuf> &&iqbuf_)
{
    iqbuf = std::move(iqbuf_);
    iqbufoff = iqbuf->delay;
    pkt.reset();
}

size_t MultichannelSynthesizer::ChannelState::upsample(void)
{
    return Upsampler::upsample(iqbuf->data() + iqbufoff,
                               iqbuf->size() - iqbufoff,
                               fdbuf->data(),
                               1.0f,
                               false,
                               nsamples,
                               delay + max_samples,
                               fdnsamples);
}

void MultichannelSynthesizer::ChannelState::flush(Slot &slot)
{
    if (nsamples < delay + max_samples)
        Upsampler::upsample(nullptr,
                            0,
                            fdbuf->data(),
                            1.0f,
                            true,
                            nsamples,
                            delay + max_samples,
                            fdnsamples);

    if (nsamples > delay + max_samples) {
        nsamples = delay + max_samples;
        npartial = nsamples % L;
    }

    if (nsamples > slot.nsamples) {
        slot.delay = delay;
        slot.nsamples = nsamples;
        slot.fdnsamples = fdnsamples;
        slot.npartial = npartial;
    }
}
