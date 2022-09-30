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

/** @brief Channel modulator for multichannel modulation */
class MultichannelSynthesizer::MultichannelModulator : public ChannelModulator {
public:
    MultichannelModulator(const PHYChannel &channel,
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
     * @param overfill true if this slot can be overfilled
     */
    void nextSlot(const Slot *prev_slot, Slot &slot, const bool overfill);

    /** @brief Determine whether or not a modulated packet will fit in
     * the current frequency domain buffer.
     * @param mpkt The modulated packet
     * @param overfill Flag that is true if the slot can be overfilled
     * @return true if the packet will fit in the slot, false otherwise
     */
    /** If the packet will fit in the slot, nsamples is updated
     * appropriately.
     */
    bool fits(ModPacket &mpkt, const bool overfill);

    /** @brief Set current IQ buffer to be upsampled.
     * @param iqbuf
     */
    void setIQBuffer(std::shared_ptr<IQBuf> &&iqbuf);

    /** @brief Calculate how many samples will be in upsampled signal.
     * @@param n Number of samples in original signal.
     * @return Number of samples in upsampled signal.
     */
    size_t upsampledSize(size_t n)
    {
        return upsampler.I*n/upsampler.X;
    }

    /** @brief Perform frequency-domain upsampling on current IQ buffer.
     * @return Number of samples read from the input buffer.
     */
    size_t upsample(void);

    /** @brief Perform frequency-domain upsampling on current IQ buffer.
     * @param slot Current slot.
     */
    void flush(Slot &slot);

    /** @brief Mutex for channel state */
    std::mutex mutex;

    /** @brief Packet whose modulated signal is the IQ buffer */
    std::shared_ptr<NetPacket> pkt;

    /** @brief IQ buffer being upsampled */
    std::shared_ptr<IQBuf> iqbuf;

    /** @brief Offset of unmodulated data in IQ buffer */
    size_t iqbufoff;

    /** @brief Frequency domain buffer into which we upsample */
    IQBuf *fdbuf;

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

    /** @brief Maximum number of time-domain samples. */
    size_t max_samples;

    /** @brief Number of time-domain samples represented by final FFT block
     * that are included in nsamples.
     */
    size_t npartial;

    /** @brief FFT buffer offset before flush of partial block. */
    std::optional<size_t> partial_fftoff;

    /** @brief Number of valid samples in the frequency-domain buffer */
    /** This will be a multiple of N */
    size_t fdnsamples;

    /** @brief Frequency domain upsampler */
    Upsampler upsampler;
};

MultichannelSynthesizer::MultichannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                 double tx_rate,
                                                 size_t nthreads)
  : SlotSynthesizer(channels, tx_rate, nthreads+1)
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
        const Schedule::slot_type &slots = schedule_[channelidx];

        // Skip this channel if we're not allowed to modulate
        if (slots[slot.slotidx]) {
            std::lock_guard<std::mutex> lock(mods_[channelidx]->mutex);

            mods_[channelidx]->flush(slot);
        }
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
        // Wait for the next slot if we are starting at the first channel for
        // which we are responsible.
        do {
            slot = std::atomic_load_explicit(&curslot_, std::memory_order_acquire);
        } while (!needs_sync() && slot == prev_slot);

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

            // Otherwise, get the next slot
            continue;
        }

        if (!slot)
            continue;

        // If we don't have a schedule yet, try again
        if (slot->slotidx > schedule_.nslots()) {
            prev_slot = std::move(slot);
            continue;
        }

        // Get the frequency-domain buffer for the slot, creating it if it does
        // not yet exist
        {
            std::lock_guard<std::mutex> lock(slot->mutex);

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
            MultichannelModulator     &mod = *mods_[channelidx];
            const Schedule::slot_type &slots = schedule_[channelidx];

            // Skip this channel if we're not allowed to modulate
            if (!slots[slot->slotidx])
                continue;

            // We can overfill if we are allowed to transmit on the same channel
            // in the next slot in the schedule
            bool overfill = schedule_.mayOverfill(channelidx, slot->slotidx);

            {
                std::lock_guard<std::mutex> lock(slot->mutex);

                if (overfill)
                    slot->max_samples = slot->full_slot_samples;
            }

            {
                std::lock_guard<std::mutex> lock(mod.mutex);

                // Modulate into a new slot
                mod.nextSlot(prev_slot.get(), *slot, overfill);

                // Do upsampling of leftover IQ buffer here
                if (mod.iqbuf) {
                    mod.iqbufoff += mod.upsample();

                    // This should never happen!
                    if (mod.iqbufoff != mod.iqbuf->size())
                        logPHY(LOGERROR, "leftover IQ buffer bigger than slot!");

                    mod.iqbuf.reset();
                    assert(mod.pkt);
                    mod.pkt.reset();
                }
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

                // If the slot is closed, bail.
                if (slot->closed.load(std::memory_order_acquire))
                    break;

                std::lock_guard<std::mutex> lock(mod.mutex);

                // Modulate the packet
                if (!mpkt->pkt) {
                    float g = channels_[channelidx].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain()*g_multichan_;

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
                        std::lock_guard<std::mutex> lock(slot->mutex);

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
                        if (mod.iqbufoff == mod.iqbuf->size()) {
                            assert(mod.iqbuf);
                            mod.iqbuf.reset();
                        } else
                            break;
                    } else
                        mpkt->samples = std::move(mod.iqbuf);
                }

                // If we didn't successfully push the packet, there are two
                // options:
                // 1) The packet is too large for any slot. In this case, drop
                //    it and try again.
                // 2) The packet is too large for the remainder of *this* slot.
                //    In this case, we are done with this slot and will attempt
                //    to add the packet to the next slot.
                if (!pushed) {
                    if (mpkt->nsamples > slot->max_samples) {
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
            std::lock_guard<std::mutex> lock(slot->mutex);

            if (!slot->closed.load(std::memory_order_acquire))
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

    // Compute gain necessary to compensate for maximum number of channels on
    // which we may simultaneously transmit.
    unsigned chancount = 0;

    for (unsigned chanidx = 0; chanidx < schedule_.nchannels(); ++chanidx) {
        auto &slots = schedule_[chanidx];

        for (unsigned slotidx = 0; slotidx < slots.size(); ++slotidx) {
            if (slots[slotidx]) {
                ++chancount;
                break;
            }
        }
    }

    if (chancount == 0)
        g_multichan_ = 1.0f;
    else
        g_multichan_ = 1.0f/static_cast<float>(chancount);

    // Now set the channels and reconfigure the channel state
    const unsigned nchannels = channels_.size();

    mods_.resize(nchannels);

    for (unsigned chanidx = 0; chanidx < nchannels; chanidx++)
        mods_[chanidx] = std::make_unique<MultichannelModulator>(channels_[chanidx],
                                                                 chanidx,
                                                                 tx_rate_);
}

void MultichannelSynthesizer::wake_dependents()
{
    Synthesizer::wake_dependents();
}

MultichannelSynthesizer::MultichannelModulator::MultichannelModulator(const PHYChannel &channel,
                                                                      unsigned chanidx,
                                                                      double tx_rate)
  : ChannelModulator(channel, chanidx, tx_rate)
  , fdbuf(nullptr)
  , delay(0)
  , nsamples(0)
  , max_samples(0)
  , npartial(0)
  , fdnsamples(0)
  , upsampler(channel.phy->getMinTXRateOversample(), tx_rate/channel.channel.bw, channel.channel.fc/tx_rate)
{
}

void MultichannelSynthesizer::MultichannelModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                                              const float g,
                                                              ModPacket &mpkt)
{
    const float g_effective = pkt->g*g;

    // Modulate the packet
    mod_->modulate(std::move(pkt), g_effective, mpkt);

    // Set channel
    mpkt.chanidx = chanidx_;
    mpkt.channel = channel_.channel;
}

void MultichannelSynthesizer::MultichannelModulator::nextSlot(const Slot *prev_slot,
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
            // partial block, so we need to rewind the FFT upsampler.
            if (partial_fftoff) {
                upsampler.fftoff = *partial_fftoff;

                nsamples = 0;
                fdnsamples = 0;
            } else {
                // Copy the previously output FFT block
                upsampler.upsampleBlock(upsampler.fft.out.data(), fdbuf->data());

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
            upsampler.reset(upsampler.X*prev_slot->npartial/upsampler.I);

            nsamples = 0;
            fdnsamples = 0;
        }

        delay = prev_slot->npartial;
        npartial = 0;
    } else {
        // If we are NOT continuing modulation of a slot, re-initialize the FFT
        // buffer. When a packet ends exactly on a slot boundary, npartial will
        // be 0, but we DO NOT want to re-initialize the upsampler. We test for
        // this case by seeing if the number of samples output in the previous
        // slot is equal to the size of the slot.
        if (prev_slot && nsamples != delay + prev_slot->full_slot_samples)
            upsampler.reset();

        nsamples = 0;
        fdnsamples = 0;
        delay = 0;
        npartial = 0;
    }
}

bool MultichannelSynthesizer::MultichannelModulator::fits(ModPacket &mpkt, const bool overfill)
{
    // This is the number of samples the upsampled signal will need
    size_t n = upsampledSize(mpkt.samples->size() - mpkt.samples->delay);

    if (nsamples + upsampler.npending() + n <= delay + max_samples ||
        (nsamples + upsampler.npending() < delay + max_samples && overfill)) {
        mpkt.start = nsamples;
        mpkt.nsamples = n;

        return true;
    } else
        return false;
}

void MultichannelSynthesizer::MultichannelModulator::setIQBuffer(std::shared_ptr<IQBuf> &&iqbuf_)
{
    iqbuf = std::move(iqbuf_);
    iqbufoff = iqbuf->delay;
    pkt.reset();
}

size_t MultichannelSynthesizer::MultichannelModulator::upsample(void)
{
    return upsampler.upsample(iqbuf->data() + iqbufoff,
                              iqbuf->size() - iqbufoff,
                              fdbuf->data() + fdnsamples,
                              1.0f,
                              false,
                              [&](size_t n) {
                                  fdnsamples += Upsampler::N;
                                  nsamples += n;
                                  return nsamples < delay + max_samples;
                              });
}

void MultichannelSynthesizer::MultichannelModulator::flush(Slot &slot)
{
    if (nsamples < delay + max_samples) {
        partial_fftoff = upsampler.fftoff;

        upsampler.upsample(nullptr,
                           0,
                           fdbuf->data() + fdnsamples,
                           1.0f,
                           true,
                           [&](size_t n) {
                               fdnsamples += Upsampler::N;
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

    if (nsamples > slot.nsamples) {
        slot.delay = delay;
        slot.nsamples = nsamples;
        slot.fdnsamples = fdnsamples;
        slot.npartial = npartial;
    }
}
