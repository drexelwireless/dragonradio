#include <functional>

#include "Logger.hh"
#include "phy/PHY.hh"
#include "phy/TDChannelizer.hh"
#include "net/Net.hh"

using namespace std::placeholders;

TDChannelizer::TDChannelizer(std::shared_ptr<Net> net,
                             std::shared_ptr<PHY> phy,
                             const Channels &channels,
                             unsigned int nthreads)
  : Channelizer(channels)
  , source(*this, nullptr, nullptr)
  , net_(net)
  , phy_(phy)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
  , logger_(logger)
{
    net_thread_ = std::thread(&TDChannelizer::netWorker, this);

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

    for (auto it = iqbufs_.begin(); it != iqbufs_.end(); ++it)
        (*it).push(buf);
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
    iqbufs_.resize(nchannels);
    chan_seqs_.resize(nchannels);

    for (unsigned i = 0; i < nchannels; i++) {
        Channel &channel = channels_[i];
        double  rate = 1.0;
        double  shift = 0.0;

        // If the RX rate hasn't been set yet, use the default rate of 1.0 and
        // no frequency shift.
        if (rx_rate_ != 0) {
            rate = getRXDownsampleRate(channel);
            shift = 2*M_PI*channel.fc/rx_rate_;
        }

        demods_[i] = std::make_unique<ChannelDemodulator>(*phy_,
                                                          taps_,
                                                          rate,
                                                          shift);
        iqbufs_[i].clear();
        chan_seqs_[i] = 0;
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

    radio_q_.stop();

    if (net_thread_.joinable())
        net_thread_.join();

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
        auto &seq = chan_seqs_[channelidx];

        received = false;

        if (iqbuf.size() == 0)
            continue;

        buf = std::move(iqbuf.front());
        assert(buf);
        iqbuf.pop();

        // Wait for the buffer to start to fill.
        while (buf->nsamples.load(std::memory_order_acquire) == 0 &&
               !buf->complete.load(std::memory_order_acquire))
            ;

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

        // Reset state if we have a discontinuity or if we're not currently
        // receiving a frame
        if (buf->seq != seq + 1 || !demod.isFrameOpen())
            demod.reset(channels_[channelidx]);

        // Record buffer sequence number
        seq = buf->seq;

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

void TDChannelizer::netWorker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!done_) {
        if (radio_q_.pop(pkt))
            source.push(std::move(pkt));
    }
}
