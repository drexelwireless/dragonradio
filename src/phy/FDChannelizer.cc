#include <functional>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

#include "phy/FDChannelizer.hh"
#include "phy/PHY.hh"

using namespace std::placeholders;

FDChannelizer::FDChannelizer(std::shared_ptr<PHY> phy,
                             double rx_rate,
                             const Channels &channels,
                             unsigned int nthreads)
  : Channelizer(phy, rx_rate, channels)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
  , logger_(logger)
{
    fft_thread_ = std::thread(&FDChannelizer::fftWorker, this);

    for (unsigned int tid = 0; tid < nthreads; ++tid)
        demod_threads_.emplace_back(std::thread(&FDChannelizer::demodWorker,
                                    this,
                                    tid));

    reconfigure();
}

FDChannelizer::~FDChannelizer()
{
    stop();
}

void FDChannelizer::push(const std::shared_ptr<IQBuf> &buf)
{
    tdbufs_.push(buf);
}

void FDChannelizer::reconfigure(void)
{
    std::lock_guard<spinlock_mutex> lock(demod_mutex_);

    // Tell workers we are reconfiguring
    reconfigure_.store(true, std::memory_order_release);

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }

    // Wait for workers to be ready for reconfiguration
    reconfigure_sync_.wait();

    // Now set the channels and reconfigure the channel state
    unsigned nchannels = channels_.size();

    demods_.resize(nchannels);
    slots_ = std::unique_ptr<ringbuffer<Slot, LOGR> []>(new ringbuffer<Slot, LOGR>[nchannels]);

    for (unsigned i = 0; i < nchannels; i++)
        demods_[i] = std::make_unique<ChannelState>(*phy_,
                                                    channels_[i].first,
                                                    channels_[i].second,
                                                    rx_rate_);

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();
}

void FDChannelizer::stop(void)
{
    done_ = true;

    wake_cond_.notify_all();

    if (fft_thread_.joinable())
        fft_thread_.join();

    for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
        if (demod_threads_[i].joinable())
            demod_threads_[i].join();
    }
}

void FDChannelizer::fftWorker(void)
{
    std::shared_ptr<IQBuf> iqbuf;
    std::shared_ptr<IQBuf> fdbuf;
    unsigned               seq = 0;
    fftw::FFT<C>           fft(N, FFTW_FORWARD, FFTW_ESTIMATE);
    size_t                 fftoff = P-1;

    while (!done_) {
        if (tdbufs_.size() == 0)
            continue;

        // Get a time-domain IQ buffer
        iqbuf = std::move(tdbufs_.front());
        assert(iqbuf);
        tdbufs_.pop();

        // Wait for the buffer to start to fill.
        iqbuf->waitToStartFilling();

        // Create a frequency-domain buffer
        fdbuf = std::make_shared<IQBuf>((iqbuf->capacity() + L - 1)*N/L);
        fdbuf->timestamp = iqbuf->timestamp;
        fdbuf->seq = iqbuf->seq;
        fdbuf->fc = iqbuf->fc;
        fdbuf->fs = iqbuf->fs;
        fdbuf->snapshot_off = iqbuf->snapshot_off;

        // Make the frequency-domain buffer available to the individual channels
        {
            std::lock_guard<spinlock_mutex> lock(demod_mutex_);
            unsigned                        nchannels = channels_.size();

            for (unsigned i = 0; i < nchannels; ++i)
                slots_[i].push({iqbuf, fdbuf});
        }

        // Reset FFT state on buffer discontinuity. We detect a discontinuity
        // via a gap in the time-domain IQ buffer sequence number.
        if (iqbuf->seq != seq + 1) {
            std::fill(fft.in.begin(), fft.in.end(), 0);
            fftoff = P-1;
        }

        seq = iqbuf->seq;

        // Perform overlap-save on input buffer as data becomes available
        bool   complete;            // Is the buffer complete?
        size_t nsamples;            // Number of samples available
        size_t needed = N - fftoff; // Number of samples needed to perform FFT
        size_t inoff = 0;           // Offset into input buffer
        size_t outoff = 0;          // Offset into output buffer

        for (;;) {
            complete = iqbuf->complete.load(std::memory_order_acquire);
            nsamples = iqbuf->nsamples.load(std::memory_order_acquire);

            // If we don't have enough samples for a full FFT, wait for more if
            // the buffer *is not* complete, or stop processing samples if the
            // buffer *is* complete.
            if (nsamples - inoff < needed) {
                if (complete)
                    break;
                else
                    continue;
            }

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
    unsigned               channelidx = tid;
    unsigned               nchannels = demods_.size();
    Slot                   prev_slot;
    Slot                   slot;
    std::optional<ssize_t> next_snapshot_off;
    bool                   received;

    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt) {
            pkt->channel = channels_[channelidx].first;
            source.push(std::move(pkt));
        }
    };

    while (!done_) {
        // If we are reconfiguring, wait until reconfiguration is done
        if (reconfigure_.load(std::memory_order_acquire)) {
            // Wait for reconfiguration to finish
            reconfigure_sync_.wait();

            // Signal that we have resumed
            reconfigure_sync_.wait();

            // Re-compute our channel index and the number of channels
            channelidx = tid;
            nchannels = demods_.size();

            // If we are unneeded, sleep
            if (tid >= nchannels) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return done_ || reconfigure_.load(std::memory_order_acquire); });

                continue;
            }
        }

        // Demodulate the next channel for which we are responsible
        auto &demod = *demods_[channelidx];

        // Get the buffer to demodulate
        {
            auto &slots = slots_[channelidx];

            received = false;

            if (slots.size() == 0)
                continue;

            slot = std::move(slots.front());
            assert(slot.iqbuf);
            assert(slot.fdbuf);
            slots.pop();
        }

        auto &fdbuf = slot.fdbuf;

        // Wait for the buffer to start to fill.
        fdbuf->waitToStartFilling();

        // When the snapshot is over, we need to record self-transmissions
        // for one more slot to ensure we record any transmission that
        // began in the last slot of the snapshot but ended in the following
        // slot.
        std::optional<ssize_t> snapshot_off;

        if (fdbuf->snapshot_off) {
            snapshot_off = fdbuf->snapshot_off;
            next_snapshot_off = *fdbuf->snapshot_off + fdbuf->size();
        } else if (next_snapshot_off) {
            snapshot_off = next_snapshot_off;
            next_snapshot_off = std::nullopt;
        }

        // Update IQ buffer sequence number
        demod.updateSeq(fdbuf->seq);

        // Timestamp the demodulated data
        demod.timestamp(fdbuf->timestamp,
                        fdbuf->snapshot_off,
                        0);

        // Demodulate the IQ buffer
        bool   complete = false; // Is the buffer complete?
        size_t ndemodulated = 0; // How many samples we've already demodulated
        size_t n = 0;

        for (;;) {
            complete = fdbuf->complete.load(std::memory_order_acquire);
            n = fdbuf->nsamples.load(std::memory_order_acquire) - ndemodulated;

            if (n != 0) {
                demod.demodulate(fdbuf->data() + ndemodulated,
                                 n,
                                 callback);

                ndemodulated += n;
            } else if (complete)
                break;
        }

        // If we received any packets, log both the previous and the current
        // slot. We then save the current slot in case we need to log it
        // later.
        if (logger_ && received && logger_->getCollectSource(Logger::kSlots)) {
            if (prev_slot.iqbuf)
                logger_->logSlot(prev_slot.iqbuf, rx_rate_);

            logger_->logSlot(slot.iqbuf, rx_rate_);

            prev_slot = std::move(slot);
        }

        // Demodulate next channel for which we are responsible
        channelidx += nchannels;
        if (channelidx >= nchannels)
            channelidx = tid;
    }
}

FDChannelizer::ChannelState::ChannelState(PHY &phy,
                                          const Channel &channel,
                                          const std::vector<C> &taps,
                                          double rx_rate)
  : channel_(channel)
  , rate_(phy.getMinRXRateOversample()*channel.bw/rx_rate)
  , X_(phy.getMinRXRateOversample())
  , D_(rx_rate/channel.bw)
  , ifft_(X_*N/D_, FFTW_BACKWARD, FFTW_ESTIMATE)
  , H_(N)
  , demod_(phy.mkDemodulator())
  , seq_(0)
{
    // Number of FFT bins to rotate
    Nrot_ = N*channel.fc/rx_rate;
    if (Nrot_ < 0)
        Nrot_ += N;

    // Compute frequency-domain filter
    fftw::FFT<C> fft(N, FFTW_FORWARD, FFTW_ESTIMATE);

    std::fill(fft.in.begin(), fft.in.end(), 0);
    assert(taps.size() <= P);
    std::copy(taps.begin(), taps.end(), fft.in.begin());
    fft.execute(fft.in.data(), H_.data());

    // Apply 1/(N*D) factor to filter since FFTW doesn't multiply by 1/N for
    // IFFT, and we need to compensate for summation during decimation.
    const C invN = 1.0/(N*D_);

    xsimd::transform(H_.begin(), H_.end(), H_.begin(),
        [&](const auto& x) { return x*invN; });
}

void FDChannelizer::ChannelState::updateSeq(unsigned seq)
{
    // Reset state if we have a discontinuity or if we're not currently
    // receiving a frame
    if (seq != seq_ + 1 || !demod_->isFrameOpen())
        reset();

    // Record buffer sequence number
    seq_ = seq;
}

void FDChannelizer::ChannelState::reset(void)
{
    demod_->reset(channel_);
    seq_ = 0;
}

void FDChannelizer::ChannelState::timestamp(const MonoClock::time_point &timestamp,
                                            std::optional<ssize_t> snapshot_off,
                                            size_t offset)
{
    demod_->timestamp(timestamp,
                      snapshot_off,
                      offset,
                      rate_);
}

void FDChannelizer::ChannelState::demodulate(const std::complex<float>* data,
                                             size_t count,
                                             std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    C temp[N];

    for (; count > 0; count -= N, data += N) {
        // Shift FFT bins as we copy into temp buffer
        std::rotate_copy(data, data + Nrot_, data + N, temp);

        // Apply filter
        xsimd::transform(temp, temp + N, H_.begin(), temp,
            [](const auto& x, const auto& y) { return x*y; });

        // Decimate by summing strides of temp buffer, placing result in IFFT
        // input buffer
        std::copy(temp, temp+N/D_, ifft_.in.begin());

        for (unsigned i = 1; i < D_; ++i)
            xsimd::transform(temp + i*N/D_,
                             temp + (i+1)*N/D_,
                             ifft_.in.begin(),
                             ifft_.in.begin(),
                [](const auto& x, const auto& y) { return x+y; });

        // Oversample if needed
        if (X_ != 1) {
            unsigned n = N/D_;

            std::copy(ifft_.in.begin() + n/2,
                      ifft_.in.begin() + n,
                      ifft_.in.begin() + X_*N/D_ - n/2);
            std::fill(ifft_.in.begin() + n/2,
                      ifft_.in.begin() + n,
                      0);
        }

        // Perform IFFT
        ifft_.execute();

        // Demodulate
        demod_->demodulate(ifft_.out.data() + X_*(P-1)/D_, X_*L/D_, callback);
    }
}
