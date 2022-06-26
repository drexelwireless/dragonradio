// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <math.h>

#include <functional>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include <pybind11/pybind11.h>

namespace py = pybind11;

#include "phy/FDChannelizer.hh"
#include "phy/PHY.hh"

FDChannelizer::FDChannelizer(const std::vector<PHYChannel> &channels,
                             double rx_rate,
                             unsigned int nthreads)
  : Channelizer(channels, rx_rate, nthreads+2)
  , nthreads_(nthreads)
  , logger_(logger)
{
    reconfigure();

    fft_thread_ = std::thread(&FDChannelizer::fftWorker, this);

    for (unsigned int tid = 0; tid < nthreads; ++tid)
        demod_threads_.emplace_back(std::thread(&FDChannelizer::demodWorker,
                                                this,
                                                tid));
}

FDChannelizer::~FDChannelizer()
{
    stop();
}

void FDChannelizer::setChannels(const std::vector<PHYChannel> &channels)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (done_)
        return;

    checkChannels(channels, rx_rate_);

    {
        scoped_sync sync(*this);

        channels_ = channels;

        reconfigure();
    }
}

void FDChannelizer::setRXRate(double rate)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (done_)
        return;

    checkChannels(channels_, rate);

    {
        scoped_sync sync(*this);

        rx_rate_ = rate;

        reconfigure();
    }
}

void FDChannelizer::push(const std::shared_ptr<IQBuf> &buf)
{
    tdbufs_.push(buf);
}

void FDChannelizer::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // Stop all IQ buffer queues
    tdbufs_.disable();

    for (unsigned int i = 0; i < slots_.size(); ++i)
        slots_[i]->disable();

    // Set done flag
    if (modify([&]() { done_ = true; })) {
        // Join on all threads
        if (fft_thread_.joinable())
            fft_thread_.join();

        for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
            if (demod_threads_[i].joinable())
                demod_threads_[i].join();
        }
    }
}

void FDChannelizer::checkChannels(const std::vector<PHYChannel> &channels, double rx_rate)
{
    for (auto&& chan : channels) {
        if (chan.channel.fc + chan.channel.bw/2 > rx_rate/2)
            throw std::range_error("Channel does not fit in available bandwidth.");

        if (chan.channel.fc - chan.channel.bw/2 < -rx_rate/2)
            throw std::range_error("Channel does not fit in available bandwidth.");

        if (fmod(rx_rate, chan.channel.bw) != 0)
            throw std::range_error("Channel bandwidth must evenly divide total bandwidth.");
    }
}

void FDChannelizer::fftWorker(void)
{
    std::shared_ptr<IQBuf>  iqbuf;
    std::shared_ptr<IQBuf>  fdbuf;
    unsigned                seq = 0;
    fftw::FFT<C>            fft(N, FFTW_FORWARD, FFTW_MEASURE);
    size_t                  fftoff = O;

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;
        }

        // Get a time-domain IQ buffer
        if (!tdbufs_.pop(iqbuf))
            continue;

        // Reset FFT state on buffer discontinuity. We detect a discontinuity
        // via a gap in the time-domain IQ buffer sequence number.
        if (iqbuf->seq != seq + 1) {
            std::fill(fft.in.begin(), fft.in.end(), 0);
            fftoff = O;
        }

        seq = iqbuf->seq;

        // Wait for the buffer to start to fill.
        iqbuf->waitToStartFilling();

        // Create a frequency-domain buffer
        fdbuf = std::make_shared<IQBuf>(N*(1 + (iqbuf->size() + L - 1)/L));
        fdbuf->timestamp = *iqbuf->timestamp;
        fdbuf->seq = iqbuf->seq;
        fdbuf->fc = iqbuf->fc;
        fdbuf->fs = iqbuf->fs;
        fdbuf->snapshot_off = iqbuf->snapshot_off;

        // Make the frequency-domain buffer available to the individual channels
        unsigned nchannels = channels_.size();

        for (unsigned i = 0; i < nchannels; ++i)
            slots_[i]->emplace(iqbuf, fdbuf, -static_cast<ssize_t>(fftoff - O));

        // Perform overlap-save on input buffer as data becomes available
        bool   complete;            // Is the buffer complete?
        size_t nsamples;            // Number of samples available
        size_t needed = N - fftoff; // Number of samples needed to perform FFT
        size_t inoff = 0;           // Offset into input buffer
        size_t outoff = 0;          // Offset into output buffer

        for (int spin_count = 0;; ++spin_count) {
            complete = iqbuf->complete.load(std::memory_order_acquire);
            nsamples = iqbuf->nsamples.load(std::memory_order_acquire);

            // If we don't have enough samples for a full FFT, wait for more if
            // the buffer *is not* complete, or stop processing samples if the
            // buffer *is* complete.
            if (nsamples - inoff < needed) {
                if (complete)
                    break;

                if (spin_count < 16)
                    _mm_pause();
                else {
                    std::this_thread::yield();
                    spin_count = 0;
                }

                continue;
            }

            spin_count = 0;

            // Use needed samples from the input buffer
            assert(fftoff + needed == N);
            std::copy(iqbuf->data() + inoff,
                      iqbuf->data() + inoff + needed,
                      fft.in.begin() + fftoff);

            // Perform the FFT
            fft.execute();

            // Copy FFT buffer to output
            std::copy(fft.out.begin(), fft.out.end(), fdbuf->data() + outoff);
            outoff += N;

            // If the FFT buffer held up to L samples, we can get all the data
            // we need for the next FFT from the input buffer.
            //
            // Otherwise we need to reuse some of the data in the current FFT
            // buffer in the next round.
            if (fftoff <= L) {
                inoff += L - fftoff;
                fftoff = 0;
                needed = N;
            } else {
                std::copy(fft.in.begin() + L,
                          fft.in.end(),
                          fft.in.begin());
                fftoff -= L;
                needed += L;
            }

            fdbuf->nsamples.store(outoff, std::memory_order_release);
        }

        // Resize the buffer
        fdbuf->resize(outoff);

        // Now the frequency domain buffer is complete
        fdbuf->complete.store(true, std::memory_order_release);

        // The rest of the input will be processed as part of the next full FFT
        // buffer
        size_t nleftover = nsamples - inoff; // Number of leftover samples

        assert(fftoff + nleftover < N);
        std::copy(iqbuf->data() + inoff,
                  iqbuf->data() + nsamples,
                  fft.in.begin() + fftoff);
        fftoff += nleftover;
    }
}

void FDChannelizer::demodWorker(unsigned tid)
{
    // We keep two past buffers when logging slots
    std::shared_ptr<IQBuf>  prev_prev_iqbuf;
    std::shared_ptr<IQBuf>  prev_iqbuf;
    Slot                    slot;
    std::optional<ssize_t>  next_snapshot_off;
    unsigned                num_extra_snapshot_slots = 0;
    bool                    received = false; // Have we received any packets?
    unsigned                chanidx;          // Index of current channel being demodulated
    Channel                 channel;          // Current channel being demodulated

    PHY::PacketDemodulator::callback_type callback = [&] (std::shared_ptr<RadioPacket> &&pkt) {
        received = true;
        if (pkt) {
            pkt->chanidx = chanidx;
            pkt->channel = channel;
            source.push(std::move(pkt));
        }
    };

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we are unneeded, sleep
            if (tid >= channels_.size()) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return needs_sync(); });

                continue;
            }

            // Set demodulator callbacks
            for (unsigned i = tid; i < channels_.size(); i += nthreads_)
                demods_[i]->setCallback(callback);
        }

        for (chanidx = tid; chanidx < channels_.size(); chanidx += nthreads_) {
            auto &demod = *demods_[chanidx];
            auto &slots = *slots_[chanidx];

            // Get a slot
            if (!slots.pop(slot))
                continue;

            auto &fdbuf = slot.fdbuf;
            auto &iqbuf = slot.iqbuf;

            // Wait for the buffer to start to fill.
            fdbuf->waitToStartFilling();

            // When the snapshot is over, we need to record self-transmissions
            // for one more slot to ensure we record any transmission that began
            // in the last slot of the snapshot but ended in the following slot.
            // The offset for the next snapshot IQ buffer was saved in
            // next_snapshot_off, so we use that if this IQ buffer does not have
            // a snapshot offset.
            std::optional<ssize_t> snapshot_off;

            if (iqbuf->snapshot_off)
                snapshot_off = iqbuf->snapshot_off;
            else
                snapshot_off = next_snapshot_off;

            // Update IQ buffer sequence number
            demod.updateSeq(fdbuf->seq);

            // Timestamp the demodulated data
            demod.timestamp(*fdbuf->timestamp,
                            snapshot_off,
                            slot.fd_offset);

            // Demodulate the IQ buffer
            bool   complete = false; // Is the buffer complete?
            size_t ndemodulated = 0; // How many samples we've already demodulated
            size_t n = 0;

            // Set parameters for the callback
            received = false;
            channel = channels_[chanidx].channel;

            // Demodulate data as it is received
            for (int spin_count = 0; !complete; ++spin_count) {
                complete = fdbuf->complete.load(std::memory_order_acquire);
                n = fdbuf->nsamples.load(std::memory_order_acquire) - ndemodulated;

                if (n != 0) {
                    demod.demodulate(fdbuf->data() + ndemodulated, n);
                    ndemodulated += n;
                    spin_count = 0;
                } else if (spin_count < 16) {
                    _mm_pause();
                } else {
                    std::this_thread::yield();
                    spin_count = 0;
                }
            }

            // Save the snapshot offset of the next IQ buffer here if we know
            // what it will be. iqbuf's size is valid now that it has been
            // marked complete.
            if (iqbuf->snapshot_off) {
                next_snapshot_off = *iqbuf->snapshot_off + iqbuf->size();
                num_extra_snapshot_slots = 2;
            } else if (num_extra_snapshot_slots > 0) {
                --num_extra_snapshot_slots;
                next_snapshot_off = *next_snapshot_off + iqbuf->size();
            } else
                next_snapshot_off = std::nullopt;

            // If we received any packets, log both the previous and the current
            // slot. We then save the current slot in case we need to log it
            // later.
            if (logger_ && logger_->getCollectSource(Logger::kSlots)) {
                if (received) {
                    if (prev_prev_iqbuf) {
                        logger_->logSlot(prev_prev_iqbuf);
                        prev_prev_iqbuf.reset();
                    }

                    if (prev_iqbuf) {
                        logger_->logSlot(prev_iqbuf);
                        prev_iqbuf.reset();
                    }

                    logger_->logSlot(slot.iqbuf);
                } else {
                    prev_prev_iqbuf = std::move(prev_iqbuf);
                    prev_iqbuf = std::move(slot.iqbuf);
                }
            }
        }
    }
}

void FDChannelizer::reconfigure(void)
{
    unsigned nchannels = channels_.size();

    // Make sure every channel has a slot queue and create a new demodulator for
    // each channel.
    demods_.resize(nchannels);
    slots_.resize(nchannels);

    for (unsigned i = 0; i < nchannels; i++) {
        if (!slots_[i])
            slots_[i] = std::make_unique<SafeQueue<Slot>>();

        demods_[i] = std::make_unique<FDChannelDemodulator>(channels_[i],
                                                            rx_rate_);
    }

    // Re-enable the slot queues
    for (unsigned i = 0; i < nchannels; i++)
        slots_[i]->enable();

    // Re-enable the FFT worker queue
    tdbufs_.enable();
}

void FDChannelizer::wake_dependents()
{
    // Disable the FFT worker queue
    tdbufs_.disable();

    // Disable the slot queues
    unsigned nchannels = channels_.size();

    for (unsigned i = 0; i < nchannels; i++)
        slots_[i]->disable();

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }
}

FDChannelizer::FDChannelDemodulator::FDChannelDemodulator(const PHYChannel &channel,
                                                          double rx_rate)
  : ChannelDemodulator(channel, rx_rate)
  , seq_(0)
  , X_(channel.phy->getMinRXRateOversample())
  , D_(rx_rate/channel.channel.bw)
  , ifft_(X_*N/D_, FFTW_BACKWARD, FFTW_MEASURE)
  , temp_(N)
  , H_(N)
{
    // Channel bandwidth must be less that total available bandwidth
    assert(channel.channel.bw <= rx_rate);

    // Channel bandwidth must evenly divide total bandwidth
    assert(fmod(rx_rate_, channel.channel.bw) == 0);

    // Number of FFT bins to rotate
    Nrot_ = N*channel.channel.fc/rx_rate;
    if (Nrot_ < 0)
        Nrot_ += N;

    // Compute frequency-domain filter
    fftw::FFT<C> fft(N, FFTW_FORWARD, FFTW_MEASURE);

    std::fill(fft.in.begin(), fft.in.end(), 0);
    assert(channel.taps.size() <= P);
    std::copy(channel.taps.begin(), channel.taps.end(), fft.in.begin());
    fft.execute(fft.in.data(), H_.data());

    // Compute filter delay
    delay_ = round((channel.taps.size() - 1) / 2.0);

    // Apply 1/(N*D) factor to filter since FFTW doesn't multiply by 1/N for
    // IFFT, and we need to compensate for summation during decimation.
    const C invN = 1.0/(N*D_);

    xsimd::transform(H_.begin(), H_.end(), H_.begin(),
        [&](const auto& x) { return x*invN; });
}

void FDChannelizer::FDChannelDemodulator::updateSeq(unsigned seq)
{
    // Reset state if we have a discontinuity or if we're not currently
    // receiving a frame
    if (seq != seq_ + 1 || !demod_->isFrameOpen())
        reset();

    // Record buffer sequence number
    seq_ = seq;
}

void FDChannelizer::FDChannelDemodulator::reset(void)
{
    demod_->reset(channel_.channel);
    seq_ = 0;
}

void FDChannelizer::FDChannelDemodulator::timestamp(const MonoClock::time_point &timestamp,
                                                    std::optional<ssize_t> snapshot_off,
                                                    ssize_t offset)
{
    demod_->timestamp(timestamp,
                      snapshot_off,
                      offset,
                      delay_,
                      rate_,
                      rx_rate_);
}

void FDChannelizer::FDChannelDemodulator::demodulate(const std::complex<float>* data,
                                                     size_t count)
{
    const unsigned n = N/D_;

    for (; count > 0; count -= N, data += N) {
        // Shift FFT bins as we copy into temp buffer
        std::rotate_copy(data, data + Nrot_, data + N, temp_.begin());

        // Apply filter
        xsimd::transform(temp_.begin(), temp_.end(), H_.begin(), temp_.begin(),
            [](const auto& x, const auto& y) { return x*y; });

        // Decimate by summing strides of temp buffer, placing result in IFFT
        // input buffer
        std::copy(temp_.begin(), temp_.begin() + n, ifft_.in.begin());

        for (unsigned i = 1; i < D_; ++i)
            xsimd::transform(temp_.begin() + i*n,
                             temp_.begin() + (i+1)*n,
                             temp_.begin(),
                             temp_.begin(),
                [](const auto& x, const auto& y) { return x+y; });

        // Oversample if needed
        if (X_ != 1) {
            std::copy(temp_.begin() + n/2,
                      temp_.begin() + n,
                      temp_.begin() + X_*n - n/2);
            std::fill(temp_.begin() + n/2,
                      temp_.begin() + n,
                      0);
        }

        // Perform IFFT
        ifft_.execute(temp_.data(), ifft_.out.data());

        // Demodulate
        demod_->demodulate(ifft_.out.data() + X_*O/D_, X_*L/D_);
    }
}
