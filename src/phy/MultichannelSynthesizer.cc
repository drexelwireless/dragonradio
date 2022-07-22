// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <atomic>
#include <mutex>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include <pybind11/pybind11.h>

namespace py = pybind11;

#include "logging.hh"
#include "phy/MultichannelSynthesizer.hh"
#include "phy/PHY.hh"
#include "stats/Estimator.hh"

/** @brief A time slot that needs to be synthesized */
struct MultichannelSynthesizer::Slot {
    Slot(const WallClock::time_point& deadline_,
         size_t deadline_delay_,
         size_t slot_samples_,
         size_t full_slot_samples_,
         size_t slotidx_)
      : slot_samples(slot_samples_)
      , full_slot_samples(full_slot_samples_)
      , slotidx(slotidx_)
      , closed(false)
      , deadline(deadline_)
      , delay(0)
      , fdnsamples(0)
      , npartial(0)
    {
        nfinished.store(0, std::memory_order_relaxed);

        txrecord.timestamp = WallClock::to_mono_time(deadline_);
        txrecord.delay = deadline_delay_;
    }

    Slot() = delete;

    ~Slot() = default;

    /** @brief Number of samples in a full slot NOT including any guard */
    const ssize_t slot_samples;

    /** @brief Number of samples in a full slot including any guard */
    const ssize_t full_slot_samples;

    /** @brief The schedule slot this slot represents */
    const size_t slotidx;

    /** @brief Number of threads who have finished with this slot */
    std::atomic<unsigned> nfinished;

    /** @brief When true, indicates that the slot is closed for further
     * samples.
     */
    bool closed;

    /** @brief Packets to transmit */
    TXRecord txrecord;

    /** @brief Slot deadline */
    WallClock::time_point deadline;

    /** @brief Number of samples to delay */
    size_t delay;

    /** @brief Frequency-domain IQ buffer */
    std::unique_ptr<IQBuf> fdbuf;

    /** @brief Number of valid samples in the frequency-domain buffer */
    size_t fdnsamples;

    /** @brief Number of samples represented by final FFT block that were
     * part of the slot.
     */
    size_t npartial;

    /** @brief The length of the slot, in samples. */
    /** Return the length of the slot, in samples. This does not include
     * delayed samples.
     */
    size_t length(void) const
    {
        return txrecord.nsamples - delay;
    }
};

/** @brief Channel modulator for multichannel modulation */
class MultichannelSynthesizer::MultichannelModulator : public ChannelModulator {
public:
    MultichannelModulator(MultichannelSynthesizer& synthesizer,
                          const PHYChannel &channel,
                          unsigned chanidx,
                          double tx_rate);
    MultichannelModulator() = delete;

    ~MultichannelModulator() = default;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  const float g,
                  ModPacket &mpkt) override;

    /** @brief Specify the next slot to modulate.
     * @param prev_slot The previous slot
     * @param slot The new slot
     * @param schedule The current schedule
     */
    void nextSlot(const Slot* prev_slot, Slot& slot, const Schedule& schedule);

    /** @brief Push modulated packet onto slot
     * @param mpkt The modulated packet
     * @param cv Condition variable for packet consumers
     * @return true if the packet was pushed, false otherwise
     */
    bool push(std::unique_ptr<ModPacket>&, Slot& slot);

    /** @brief Perform frequency-domain upsampling on current IQ buffer.
     * @param slot Current slot.
     */
    void flush(Slot& slot);

    /** @brief Determine whether or not a modulated packet will fit in
     * the current frequency domain buffer.
     * @param mpkt The modulated packet
     * @return true if the packet will fit in the slot, false otherwise
     */
    bool fits(ModPacket& mpkt);

    /** @brief Does this modulator have samples for the next slot? */
    bool continued(void) const
    {
        return (bool) iqbuf;
    }

    /** @brief Perform frequency-domain upsampling on current IQ buffer.
     * @return Number of samples read from the input buffer.
     */
    size_t upsample(void);

private:
    /** @brief The modulator's synthesizer */
    MultichannelSynthesizer& synthesizer_;

    /** @brief IQ buffer being upsampled */
    std::shared_ptr<IQBuf> iqbuf;

    /** @brief Offset of unmodulated data in IQ buffer */
    size_t iqbufoff;

    /** @brief Number of time domain samples in the frequency domain buffer
     * to delay.
     */
    size_t delay;

    /** @brief Number of valid time-domain samples represented by data in
     * the frequency domain buffer.
     */
    /** This represents the number of valid UN-overlapped time-domain
     * samples represented by the FFT blocks in the frequency domain buffer.
     */
    size_t nsamples;

    /** @brief Can we overfill? */
    bool overfill;

    /** @brief Maximum number of time-domain samples. */
    size_t max_samples;

    /** @brief Number of time-domain samples represented by final FFT block
     * that are included in nsamples.
     */
    size_t npartial;

    /** @brief FFT buffer offset before flush of partial block. */
    std::optional<size_t> partial_fftoff;

    /** @brief Frequency domain buffer into which we upsample */
    IQBuf* fdbuf;

    /** @brief Number of valid samples in the frequency-domain buffer */
    /** This will be a multiple of N */
    size_t fdnsamples;

    /** @brief Frequency domain resampler */
    Resampler resampler;
};

MultichannelSynthesizer::MultichannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                 double tx_rate,
                                                 size_t nthreads)
  : Synthesizer(channels, tx_rate, nthreads+1)
  , enabled_(true)
  , nthreads_(nthreads)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&MultichannelSynthesizer::modWorker,
                                              this,
                                              i));

    modify([&]() { reconfigure(); });
}

MultichannelSynthesizer::~MultichannelSynthesizer()
{
    stop();
}

std::optional<size_t> MultichannelSynthesizer::getHighWaterMark(void) const
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    return high_water_mark_;
}

void MultichannelSynthesizer::setHighWaterMark(std::optional<size_t> high_water_mark)
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    high_water_mark_ = high_water_mark;
}

bool MultichannelSynthesizer::isEnabled(void) const
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    return enabled_;
}

void MultichannelSynthesizer::enable(void)
{
    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        enabled_ = true;
    }

    consumer_cv_.notify_all();
}

void MultichannelSynthesizer::disable(void)
{
    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        enabled_ = false;
    }

    consumer_cv_.notify_all();
}

TXRecord MultichannelSynthesizer::try_pop(void)
{
    return TXRecord{};
}

TXRecord MultichannelSynthesizer::pop(void)
{
    std::shared_ptr<Slot> slot;

    push_slot();

    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        consumer_cv_.wait(lock, [&]{ return !enabled_ || !curslot_ || !curslot_->txrecord.mpkts.empty(); });

        if (enabled_)
            slot = std::move(curslot_);
    }

    if (slot) {
        closeAndFinalize(*slot);
        return std::move(slot->txrecord);
    } else {
        return TXRecord{};
    }
}

TXRecord MultichannelSynthesizer::pop_for(const std::chrono::duration<double>& rel_time)
{
    std::shared_ptr<Slot> slot;

    push_slot();

    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        consumer_cv_.wait(lock, [&]{ return !enabled_ || !curslot_ || !curslot_->txrecord.mpkts.empty(); });

        if (enabled_)
            slot = std::move(curslot_);
    }

    if (slot) {
        closeAndFinalize(*slot);
        return std::move(slot->txrecord);
    } else {
        return TXRecord{};
    }
}

void MultichannelSynthesizer::push_slot(const WallClock::time_point& when, size_t slotidx, ssize_t prev_oversample)
{
    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        curslot_ = std::make_shared<Slot>(when,
                                          prev_oversample,
                                          tx_slot_samps_ - prev_oversample,
                                          tx_full_slot_samps_ - prev_oversample,
                                          slotidx);
    }

    producer_cv_.notify_all();
}

TXSlot MultichannelSynthesizer::pop_slot(void)
{
    std::shared_ptr<Slot> slot;

    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        slot = std::move(curslot_);
    }

    if (!slot)
        return TXSlot{};

    // Close and finalize the slot.
    closeAndFinalize(*slot);

    // Return TXSlot
    return TXSlot { std::move(slot->txrecord)
                  , std::move(slot->deadline)
                  , static_cast<ssize_t>(slot->length() - slot->full_slot_samples)
                  , schedule_.canTransmitInSlot((slot->slotidx + 1) % schedule_.nslots())
                  };
}

bool MultichannelSynthesizer::push(std::unique_ptr<ModPacket>& mpkt,
                                   Slot& slot)
{
    {
        std::lock_guard<std::mutex> lock(curslot_mutex_);

        // If the slot is closed, put the IQ buffer back into the ModPacket and
        // return false since we failed to push.
        if (slot.closed)
            return false;

        slot.txrecord.mpkts.emplace_back(std::move(mpkt));
    }

    // Signal any consumer
    consumer_cv_.notify_one();

    return true;
}

void MultichannelSynthesizer::push_slot(void)
{
    WallClock::time_point t_now = WallClock::now();
    WallClock::time_point when = t_now - schedule_.slotOffsetAt(t_now);
    size_t                slotidx = schedule_.slotAt(t_now);

    if (schedule_.nslots() == 0)
        return;

    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        curslot_ = std::make_shared<Slot>(when,
                                          0,
                                          tx_slot_samps_,
                                          tx_full_slot_samps_,
                                          slotidx);
    }

    producer_cv_.notify_all();
}

void MultichannelSynthesizer::closeAndFinalize(Slot& slot)
{
    // Close the slot. We grab the slot's mutex to guarantee that all
    // synthesizer threads have seen that the slot is closed---this serves as a
    // barrier. After this, no synthesizer will touch the slot, so we are
    // guaranteed exclusive access.
    {
        std::lock_guard<std::mutex> lock(curslot_mutex_);

        slot.closed = true;
    }

    // Finalize the slot
    finalize(slot);
}

void MultichannelSynthesizer::finalize(Slot &slot)
{
    if (!slot.fdbuf)
        return;

    // If we've already converted the frequency-domain buffer to a time-domain
    // buffer, there's nothing left to do.
    if (!slot.txrecord.iqbufs.empty())
        return;

    // Flush all synthesis state
    size_t nchannels = mods_.size();

    for (unsigned channelidx = 0; channelidx < nchannels; ++channelidx) {
        const Schedule::slot_type &slots = schedule_[channelidx];

        // Skip this channel if we're not allowed to modulate
        if (slots[slot.slotidx]) {
            std::lock_guard<std::mutex> lock(mod_mutexes_[channelidx]);

            mods_[channelidx]->flush(slot);
        }
    }

    // If we have any samples, our delay will always be less than nsamples, so
    // we can just check that nsamples is non-zero here.
    if (slot.txrecord.nsamples == 0)
        return;

    // Convert the frequency-domain signal back to the time domain
    auto iqbuf = std::make_shared<IQBuf>(L*(slot.fdnsamples/N));

    assert(slot.fdnsamples <= slot.fdbuf->size());
    assert(slot.fdnsamples % N == 0);

    timedomain_.toTimeDomain(slot.fdbuf->data(), slot.fdnsamples, iqbuf->data());
    iqbuf->delay = slot.delay;
    iqbuf->resize(slot.txrecord.nsamples);

    slot.txrecord.iqbufs.emplace_back(std::move(iqbuf));
}

void MultichannelSynthesizer::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    // Set done flag
    if (modify([&](){ done_ = true; })) {
        // Join on all threads
        for (size_t i = 0; i < mod_threads_.size(); ++i) {
            if (mod_threads_[i].joinable())
                mod_threads_[i].join();
        }
    }
}

void MultichannelSynthesizer::modWorker(unsigned tid)
{
    std::shared_ptr<Slot>      prev_slot;
    std::shared_ptr<Slot>      slot;
    std::unique_ptr<ModPacket> mpkt;
    std::shared_ptr<NetPacket> pkt;

    for (;;) {
        // Get the next slot
        {
            std::unique_lock<std::mutex> lock(curslot_mutex_);

            producer_cv_.wait(lock, [&]{ return needs_sync() || curslot_ != prev_slot; });

            slot = curslot_;
        }

        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                break;

            // If we are unneeded, sleep
            if (schedule_.nchannels() == 0 || tid >= channels_.size()) {
                sleep_until_state_change();
                continue;
            }

            continue;
        }

        // If we don't have a new slot, try again
        if (!slot || slot == prev_slot)
            continue;

        // If we don't have a schedule yet, try again
        if (slot->slotidx > schedule_.nslots()) {
            prev_slot = std::move(slot);
            continue;
        }

        // Get the frequency-domain buffer for the slot, creating it if it does
        // not yet exist
        {
            std::lock_guard<std::mutex> lock(curslot_mutex_);

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

        for (unsigned channelidx = tid; channelidx < channels_.size(); channelidx += nthreads_) {
            // Get channel state for current channel
            std::mutex                &mod_mutex = mod_mutexes_[channelidx];
            MultichannelModulator     &mod = *mods_[channelidx];
            const Schedule::slot_type &slots = schedule_[channelidx];

            // Skip this channel if we're not allowed to modulate
            if (!slots[slot->slotidx])
                continue;

            {
                std::lock_guard<std::mutex> lock(mod_mutex);

                // Modulate into a new slot
                mod.nextSlot(prev_slot.get(), *slot, schedule_);
            }

            // Modulate packets for the current slot
            for (;;) {
                // If we need to synchronize on state change, break to continue
                // in outer loop
                if (needs_sync())
                    break;

                // If we don't have a modulated packet already, get a packet to
                // modulate (if needed), and then create a ModPacket for modulation.
                if (!mpkt) {
                    if (!pkt) {
                        if (!sink.pull(pkt))
                            continue;
                    }

                    mpkt = std::make_unique<ModPacket>();
                }

                std::lock_guard<std::mutex> lock(mod_mutex);

                // Modulate the packet
                if (!mpkt->pkt) {
                    float g = channels_[channelidx].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain()*g_multichan_;

                    mod.modulate(std::move(pkt), g, *mpkt);
                }

                // If we can push the packet onto the slot, break if the packet
                // must be continued into the next slot.
                if (mod.push(mpkt, *slot)) {
                    if (mod.continued())
                        break;
                } else {
                    // If we didn't successfully push the packet, there are two
                    // options:
                    //
                    // 1) The packet is too large for any slot. In this case,
                    //    drop it and try again.
                    //
                    // 2) The packet is too large for the remainder of *this*
                    //    slot. In this case, we are done with this slot and
                    //    will attempt to add the packet to the next slot.
                    if (mpkt->nsamples > tx_slot_samps_) {
                        logPHY(LOGWARNING, "Modulated packet is larger than slot!");
                        mpkt.reset();
                    } else {
                        pkt = std::move(mpkt->pkt);
                        mpkt.reset();
                        break;
                    }
                }
            }
        }

        // We are done with this slot. Finalize it if everyone else has finished
        // too.
        if (slot->nfinished.fetch_add(1, std::memory_order_acq_rel) == nthreads_ - 1) {
            std::lock_guard<std::mutex> lock(curslot_mutex_);

            if (!slot->closed)
                finalize(*slot);
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

void MultichannelSynthesizer::reconfigure(void)
{
    Synthesizer::reconfigure();

    // Compute number of samples in slot
    const auto slot_size = schedule_.getSlotSize();
    const auto guard_size = schedule_.getGuardSize();

    tx_slot_samps_ = tx_rate_*(slot_size - guard_size).count();
    tx_full_slot_samps_ = tx_rate_*slot_size.count();

    // Compute gain necessary to compensate for maximum number of channels on
    // which we may simultaneously transmit.
    size_t max_chancount = 0;

    if (schedule_.nchannels() != 0) {
        std::vector<size_t> chancount(schedule_.nslots());

        for (unsigned chanidx = 0; chanidx < schedule_.nchannels(); ++chanidx) {
            auto &slots = schedule_[chanidx];

            for (unsigned slotidx = 0; slotidx < slots.size(); ++slotidx) {
                if (slots[slotidx]) {
                    ++chancount[slotidx];
                    break;
                }
            }
        }

        max_chancount = *std::max_element(chancount.begin(), chancount.end());
    }

    if (max_chancount == 0)
        g_multichan_ = 1.0f;
    else
        g_multichan_ = 1.0f/static_cast<float>(max_chancount);

    // Create modulators
    const unsigned nchannels = channels_.size();

    mod_mutexes_ = std::vector<std::mutex>(nchannels);
    mods_.resize(nchannels);

    for (unsigned chanidx = 0; chanidx < nchannels; chanidx++)
        mods_[chanidx] = std::make_unique<MultichannelModulator>(*this,
                                                                 channels_[chanidx],
                                                                 chanidx,
                                                                 tx_rate_);
}

void MultichannelSynthesizer::wake_dependents()
{
    // Wake threads waiting on the current slot
    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        producer_cv_.notify_all();
    }

    Synthesizer::wake_dependents();
}

MultichannelSynthesizer::MultichannelModulator::MultichannelModulator(MultichannelSynthesizer& synthesizer,
                                                                      const PHYChannel& channel,
                                                                      unsigned chanidx,
                                                                      double tx_rate)
  : ChannelModulator(channel, chanidx, tx_rate)
  , synthesizer_(synthesizer)
  , delay(0)
  , nsamples(0)
  , overfill(false)
  , max_samples(0)
  , npartial(0)
  , fdbuf(nullptr)
  , fdnsamples(0)
  , resampler(channel.I,
              channel.D,
              channel.phy->getTXOversampleFactor(),
              channel.channel.fc/tx_rate,
              channel.taps)
{
    resampler.setParallelizable(true);
    resampler.setExact(true);
}

void MultichannelSynthesizer::MultichannelModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                                              const float g,
                                                              ModPacket& mpkt)
{
    const float g_effective = pkt->g*g;

    // Modulate the packet
    mod_->modulate(std::move(pkt), g_effective, mpkt);

    // Set channel
    mpkt.chanidx = chanidx_;
    mpkt.channel = channel_.channel;
}

void MultichannelSynthesizer::MultichannelModulator::nextSlot(const Slot* prev_slot,
                                                              Slot& slot,
                                                              const Schedule& schedule)
{
    // It's safe to keep a plain old pointer since we will only keep this
    // pointer around as long as we have a reference to the slot, and the slot
    // owns this buffer.
    fdbuf = slot.fdbuf.get();

    // Determine maximum number of samples we can push into this slot. The
    // slot's TXRecord's delay is the number of overflow samples from the
    // previous slot, which we must subtract from the number of samples in the
    // current slot.
    if (synthesizer_.high_water_mark_) {
        max_samples = *synthesizer_.high_water_mark_;
    } else {
        overfill = schedule.mayOverfill(chanidx_, slot.slotidx);
        max_samples = overfill ? slot.full_slot_samples : slot.slot_samples;
    }

    // Was a partial block output in the previous slot?
    if (prev_slot && prev_slot->npartial != 0) {
        if (npartial != 0) {
            // We output a partial FFT block for the previous slot. There are
            // two ways we may end up outputting a partial block:
            //  1. We output a full upsampled block, but only part of it will
            //     fit in the current slot.
            //  2. We flushed the current upsampling buffer with zeroes, in
            //     which case we'd like to "rewind" our FFT buffer to replace
            //     the zeros with actual signal to avoid wasting space.

            // Any channel that outputs a partial block will have the same
            // number of partial samples.
            assert(npartial == prev_slot->npartial);

            // If partial_fftoff is set, we flushed our FFT buffer to yield a
            // partial block, so we need to rewind the FFT resampler.
            if (partial_fftoff) {
                resampler.restoreFFTOffset(*partial_fftoff);

                nsamples = 0;
                fdnsamples = 0;
            } else {
                // Copy the previously output FFT block
                resampler.copyFFTOut(fdbuf->data());

                // We start with a full FFT block of samples
                nsamples = L;
                fdnsamples = N;
            }
        } else {
            // We didn't output a partial block, but somebody else did. Our
            // first prev_slot->npartial samples must be zero to account for the
            // fact that we didn't output any signal for the final
            // prev_slot->npartial samples of the previous slot.

            // This sets up the FFT buffer so that the first prev_slot->npartial
            // samples we output will be zero.
            resampler.reset(prev_slot->npartial/resampler.getRate());

            nsamples = 0;
            fdnsamples = 0;
        }

        delay = prev_slot->npartial;
        npartial = 0;
    } else {
        // If we are NOT continuing modulation of a slot, re-initialize the FFT
        // buffer. When a packet ends exactly on a slot boundary, npartial will
        // be 0, but we DO NOT want to re-initialize the resampler. We test for
        // this case by seeing if the number of samples output in the previous
        // slot is equal to the size of the slot.
        if (prev_slot && nsamples != delay + prev_slot->full_slot_samples)
            resampler.reset();

        nsamples = 0;
        fdnsamples = 0;
        delay = 0;
        npartial = 0;
    }

    // Do upsampling of leftover IQ buffer here
    if (iqbuf) {
        iqbufoff += upsample();

        // This should never happen!
        if (iqbufoff != iqbuf->size())
            logPHY(LOGERROR, "leftover IQ buffer bigger than slot!");

        iqbuf.reset();
    }
}

bool MultichannelSynthesizer::MultichannelModulator::push(std::unique_ptr<ModPacket>& mpkt,
                                                          Slot& slot)
{
    if (!fits(*mpkt))
        return false;

    // The ModPacket's IQ buffer now belongs to us
    iqbuf = std::move(mpkt->samples);
    mpkt->offset = nsamples;
    mpkt->nsamples = resampler.resampledSize(iqbuf->size() - iqbuf->delay);

    // Push onto slot
    if (!synthesizer_.push(mpkt, slot)) {
        mpkt->samples = std::move(iqbuf);
        return false;
    }

    // If we pushed the packet, record the new offset into the IQ buffer. Since
    // iqbufoff is used by upsample, we must set it *before* we call upsample.
    iqbufoff = iqbuf->delay;
    iqbufoff += upsample();

    // If the entire packet fit in the slot, free the IQ buffer. Otherwise, keep
    // the buffer around so we can put the rest of it into the next slot.
    if (iqbufoff == iqbuf->size())
        iqbuf.reset();

    return true;
}

void MultichannelSynthesizer::MultichannelModulator::flush(Slot &slot)
{
    if (nsamples < delay + max_samples) {
        partial_fftoff = resampler.saveFFTOffset();

        resampler.resampleToFD(nullptr,
                               0,
                               fdbuf->data() + fdnsamples,
                               1.0f,
                               true,
                               [&](size_t n) {
                                   fdnsamples += Resampler::N;
                                   nsamples += n;

                                   return nsamples < delay + max_samples;
                               });
    } else
        partial_fftoff = std::nullopt;

    if (nsamples > delay + max_samples) {
        nsamples = delay + max_samples;
        npartial = nsamples % L;
    } else
        npartial = 0;

    if (nsamples > slot.txrecord.nsamples) {
        slot.delay = delay;
        slot.txrecord.nsamples = nsamples;
        slot.fdnsamples = fdnsamples;
        slot.npartial = npartial;
    }
}

bool MultichannelSynthesizer::MultichannelModulator::fits(ModPacket& mpkt)
{
    if (synthesizer_.high_water_mark_) {
        return nsamples <= *synthesizer_.high_water_mark_;
    } else {
        // This is the number of samples the upsampled signal will need
        const size_t n = resampler.resampledSize(mpkt.samples->size() - mpkt.samples->delay);

        return (nsamples + resampler.npending() + n <= delay + max_samples)
            || (nsamples + resampler.npending() < delay + max_samples && overfill);
    }
}

size_t MultichannelSynthesizer::MultichannelModulator::upsample(void)
{
    return resampler.resampleToFD(iqbuf->data() + iqbufoff,
                                  iqbuf->size() - iqbufoff,
                                  fdbuf->data() + fdnsamples,
                                  1.0f,
                                  false,
                                  [&](size_t n) {
                                      fdnsamples += Resampler::N;
                                      nsamples += n;
                                      return nsamples < delay + max_samples;
                                  });
}
