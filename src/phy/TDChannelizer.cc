#include <functional>

#include "Logger.hh"
#include "phy/PHY.hh"
#include "phy/TDChannelizer.hh"
#include "net/Net.hh"

using namespace std::placeholders;

TDChannelizer::TDChannelizer(std::shared_ptr<Net> net,
                             std::shared_ptr<PHY> phy,
                             double rx_rate,
                             const Channels &channels,
                             unsigned int nthreads)
  : Channelizer(rx_rate, channels)
  , source(*this, nullptr, nullptr)
  , net_(net)
  , phy_(phy)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
  , logger_(logger)
{
    for (unsigned int tid = 0; tid < nthreads; ++tid)
        demod_threads_.emplace_back(std::thread(&TDChannelizer::demodWorker,
                                    this,
                                    tid));

    reconfigure();
}

TDChannelizer::~TDChannelizer()
{
    stop();
}

void TDChannelizer::setChannels(const Channels &channels)
{
    Channelizer::setChannels(channels);
    reconfigure();
}

void TDChannelizer::push(const std::shared_ptr<IQBuf> &buf)
{
    std::lock_guard<spinlock_mutex> lock(demod_mutex_);
    unsigned                        nchannels = channels_.size();

    for (unsigned i = 0; i < nchannels; ++i)
        iqbufs_[i].push(buf);
}

void TDChannelizer::reconfigure(void)
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
    iqbufs_ = std::unique_ptr<ringbuffer<std::shared_ptr<IQBuf>, LOGN> []>(new ringbuffer<std::shared_ptr<IQBuf>, LOGN>[nchannels]);

    for (unsigned i = 0; i < nchannels; i++) {
        Channel &channel = channels_[i];

        demods_[i] = std::make_unique<ChannelState>(*phy_,
                                                    channel,
                                                    taps_,
                                                    rx_rate_);
    }

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();
}

void TDChannelizer::stop(void)
{
    done_ = true;

    wake_cond_.notify_all();

    for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
        if (demod_threads_[i].joinable())
            demod_threads_[i].join();
    }
}

void TDChannelizer::demodWorker(unsigned tid)
{
    unsigned               channelidx = tid;
    unsigned               nchannels = demods_.size();
    std::shared_ptr<IQBuf> prev_buf;
    std::shared_ptr<IQBuf> buf;
    IQBuf                  resamp_buf(0);
    std::optional<size_t>  next_snapshot_off;
    bool                   received;

    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt) {
            pkt->channel = channels_[channelidx];
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
        auto &iqbuf = iqbufs_[channelidx];

        received = false;

        if (iqbuf.size() == 0)
            continue;

        buf = std::move(iqbuf.front());
        assert(buf);
        iqbuf.pop();

        // Wait for the buffer to start to fill.
        buf->waitToStartFilling();

        // When the snapshot is over, we need to record self-transmissions
        // for one more slot to ensure we record any transmission that
        // began in the last slot of the snapshot but ended in the following
        // slot.
        std::optional<size_t> snapshot_off;

        if (buf->snapshot_off) {
            snapshot_off = buf->snapshot_off;
            next_snapshot_off = *buf->snapshot_off + buf->size();
        } else if (next_snapshot_off) {
            snapshot_off = next_snapshot_off;
            next_snapshot_off = std::nullopt;
        }

        // Update IQ buffer sequence number
        demod.updateSeq(buf->seq);

        // Timestamp the demodulated data
        demod.timestamp(buf->timestamp,
                        buf->snapshot_off,
                        0);

        // Demodulate the IQ buffer
        bool   complete = false; // Is the buffer complete?
        size_t ndemodulated = 0; // How many samples we've already demodulated
        size_t n = 0;

        for (;;) {
            complete = buf->complete.load(std::memory_order_acquire);
            n = buf->nsamples.load(std::memory_order_acquire) - ndemodulated;

            if (n != 0) {
                demod.demodulate(resamp_buf,
                                 buf->data() + ndemodulated,
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
            if (prev_buf)
                logger_->logSlot(prev_buf, rx_rate_);

            logger_->logSlot(buf, rx_rate_);

            prev_buf = std::move(buf);
        }

        // Demodulate next channel for which we are responsible
        channelidx += nchannels;
        if (channelidx >= nchannels)
            channelidx = tid;
    }
}

TDChannelizer::ChannelState::ChannelState(PHY &phy,
                                          const Channel &channel,
                                          const std::vector<C> &taps,
                                          double rx_rate)
  : channel_(channel)
  , rate_(phy.getMinRXRateOversample()*channel.bw/rx_rate)
  , rad_(2*M_PI*channel.fc/rx_rate)
  , resamp_(rate_, taps)
  , demod_(phy.mkDemodulator())
  , seq_(0)
{
    resamp_.setFreqShift(rad_);
}

void TDChannelizer::ChannelState::updateSeq(unsigned seq)
{
    // Reset state if we have a discontinuity or if we're not currently
    // receiving a frame
    if (seq != seq_ + 1 || !demod_->isFrameOpen())
        reset();

    // Record buffer sequence number
    seq_ = seq;
}

/** @brief Reset internal state */
void TDChannelizer::ChannelState::reset(void)
{
    resamp_.reset();
    demod_->reset(channel_);
    seq_ = 0;
}

void TDChannelizer::ChannelState::timestamp(const MonoClock::time_point &timestamp,
                                            std::optional<size_t> snapshot_off,
                                            size_t offset)
{
    demod_->timestamp(timestamp,
                      snapshot_off,
                      offset,
                      rate_);
}

void TDChannelizer::ChannelState::demodulate(IQBuf &resamp_buf,
                                             const std::complex<float>* data,
                                             size_t count,
                                             std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    if (rad_ != 0.0 || rate_ != 1.0) {
        // Resample. Note that we can't very well mix without a frequency shift,
        // so we are guaranteed that the resampler's rate is not 1 here.
        unsigned nw;

        resamp_buf.resize(resamp_.neededOut(count));
        nw = resamp_.resampleMixDown(data, count, resamp_buf.data());
        resamp_buf.resize(nw);

        // Demodulate resampled data.
        demod_->demodulate(resamp_buf.data(), resamp_buf.size(), callback);
    } else
        demod_->demodulate(data, count, callback);
}
