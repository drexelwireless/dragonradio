#include <functional>

#include "phy/PHY.hh"
#include "phy/OverlapTDChannelizer.hh"
#include "net/Net.hh"

using namespace std::placeholders;

OverlapTDChannelizer::OverlapTDChannelizer(std::shared_ptr<Net> net,
                                           std::shared_ptr<PHY> phy,
                                           double rx_rate,
                                           const Channels &channels,
                                           unsigned int nthreads)
  : Channelizer(rx_rate, channels)
  , source(*this, nullptr, nullptr)
  , net_(net)
  , phy_(phy)
  , taps_{1.0}
  , slot_size_(0.0)
  , prev_demod_(0.0)
  , prev_demod_samps_(0)
  , cur_demod_(0.0)
  , cur_demod_samps_(0)
  , enforce_ordering_(false)
  , done_(false)
  , iq_size_(0)
  , iq_next_channel_(0)
  , demod_reconfigure_(nthreads)
  , logger_(logger)
{
    net_thread_ = std::thread(&OverlapTDChannelizer::netWorker, this);

    for (unsigned int i = 0; i < nthreads; ++i) {
        demod_reconfigure_[i].store(false, std::memory_order_relaxed);
        demod_threads_.emplace_back(std::thread(&OverlapTDChannelizer::demodWorker,
                                    this,
                                    std::ref(demod_reconfigure_[i])));
    }
}

OverlapTDChannelizer::~OverlapTDChannelizer()
{
    stop();
}

void OverlapTDChannelizer::setChannels(const Channels &channels)
{
    Channelizer::setChannels(channels);

    std::lock_guard<std::mutex> lock(iq_mutex_);

    if (iq_next_channel_ >= channels_.size())
        nextWindow();
}

void OverlapTDChannelizer::push(const std::shared_ptr<IQBuf> &buf)
{
    // Push the packet on the end of the queue
    {
        std::lock_guard<std::mutex> lock(iq_mutex_);

        iq_.push_back(buf);
        ++iq_size_;
    }

    // Signal anyone waiting on the queue
    iq_cond_.notify_one();
}

void OverlapTDChannelizer::reconfigure(void)
{
    prev_demod_samps_ = prev_demod_*rx_rate_;
    cur_demod_samps_ = cur_demod_*rx_rate_;

    for (auto &flag : demod_reconfigure_)
        flag.store(true, std::memory_order_relaxed);
}

void OverlapTDChannelizer::stop(void)
{
    done_ = true;

    iq_cond_.notify_all();

    radio_q_.stop();

    if (net_thread_.joinable())
        net_thread_.join();

    for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
        if (demod_threads_[i].joinable())
            demod_threads_[i].join();
    }
}

void OverlapTDChannelizer::demodWorker(std::atomic<bool> &reconfig)
{
    ChannelState              demod(*phy_, taps_, 1.0, 0.0);
    RadioPacketQueue::barrier b;
    unsigned                  channelidx;
    std::shared_ptr<IQBuf>    buf1;
    std::shared_ptr<IQBuf>    buf2;
    IQBuf                     resamp_buf(0);
    bool                      received;

    auto callback = [&] (std::unique_ptr<RadioPacket> pkt) {
        received = true;
        if (pkt) {
            if (enforce_ordering_)
                radio_q_.push(b, std::move(pkt));
            else
                source.push(std::move(pkt));
        }
    };

    while (!done_) {
        if (!pop(b, channelidx, buf1, buf2))
            break;

        const Channel &channel = channels_[channelidx];

        received = false;

        // Calculate how many samples we want to demodulate from the tail end of
        // the previous slot
        size_t buf1_nsamples = buf1->oversample + prev_demod_samps_;

        // This can happen when the user has set a large demodulation overlap
        if (buf1_nsamples > buf1->size())
            buf1_nsamples = buf1->size();

        // Calculate offset into buf1 at which we begin demodulation
        size_t buf1_off = buf1->size() - buf1_nsamples;

        // Reconfigure if necessary
        if (reconfig.load(std::memory_order_relaxed)) {
            demod.setTaps(taps_);
            demod.setRate(getRXDownsampleRate(channel));
            demod.setFreqShift(2*M_PI*channel.fc/rx_rate_);

            reconfig.store(false, std::memory_order_relaxed);
        } else {
            demod.setFreqShift(2*M_PI*channel.fc/rx_rate_);
        }

        // Reset the state of the demodulator
        demod.reset(channel);

        // Demodulate the last part of the guard interval of the previous slots
        demod.timestamp(buf1->timestamp,
                        buf1->snapshot_off,
                        buf1_off);

        demod.demodulate(resamp_buf,
                         buf1->data() + buf1_off,
                         buf1_nsamples,
                         callback);

        // Wait for the second buffer to start to fill. If demodulation is very
        // fast, it is possible for us to finish demodulating the first buffer
        // before the second begins to fill! This actually happens with OFDM.
        while (buf2->nsamples.load(std::memory_order_acquire) == 0 &&
               !buf2->complete.load(std::memory_order_acquire))
            ;

        if (cur_demod_samps_ > buf2->undersample) {
            // Calculate how many samples from the current slot we want to
            // demodulate. We do not demodulate the tail end of the guard interval.
            bool   complete = false; // Is the buffer complete?
            size_t ndemodulated = 0; // How many samples we've already demodulated
            size_t nwanted;          // How many samples we still want to demodulate.
            size_t n = 0;

            // When the snapshot is over, we need to record self-transmissions
            // for one more slot to ensure we record any transmission that
            // began in the last slot of the snapshot but ended in the following
            // slot.
            std::optional<size_t> snapshot_off;

            if (buf2->snapshot_off)
                snapshot_off = buf2->snapshot_off;
            else if (buf1->snapshot_off)
                snapshot_off = *buf1->snapshot_off + buf1->size();

            demod.timestamp(buf2->timestamp,
                            snapshot_off,
                            0);

            nwanted = cur_demod_samps_ - buf2->undersample;

            for (;;) {
                complete = buf2->complete.load(std::memory_order_acquire);
                n = std::min(buf2->nsamples.load(std::memory_order_acquire) - ndemodulated, nwanted);

                if (n != 0) {
                    demod.demodulate(resamp_buf,
                                     &(*buf2)[ndemodulated],
                                     n,
                                     callback);

                    ndemodulated += n;
                    nwanted -= n;

                    if (nwanted == 0)
                        break;
                } else if (complete)
                    break;
            }
        }

        // Remove the barrier since we are done producing packets
        radio_q_.eraseBarrier(b);

        // If we received any packets, log both slots.
        if (logger_ && received && logger_->getCollectSource(Logger::kSlots)) {
            logger_->logSlot(buf1, rx_rate_);
            logger_->logSlot(buf2, rx_rate_);
        }
    }
}

void OverlapTDChannelizer::netWorker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!done_) {
        if (radio_q_.pop(pkt))
            source.push(std::move(pkt));
    }
}

bool OverlapTDChannelizer::pop(RadioPacketQueue::barrier& b,
                               unsigned &channel,
                               std::shared_ptr<IQBuf>& buf1,
                               std::shared_ptr<IQBuf>& buf2)
{
    static MonoClock::time_point last_overflow_log(0.0);

    // Acquire the previous slot and the current slot, removing the previous
    // slot from the queue since we no longer need it.
    std::unique_lock<std::mutex> lock(iq_mutex_);

    iq_cond_.wait(lock, [this]{ return done_ || iq_size_ > 1; });
    if (done_)
        return false;

    if (iq_size_ > 8) {
        MonoClock::time_point now = MonoClock::now();

        if ((now - last_overflow_log).get_full_secs() >= 1) {
            logEvent("PHY: Large demodulation queue: size=%lu",
                iq_size_);
            last_overflow_log = now;
        }
    }

    auto it = iq_.begin();

    b = radio_q_.pushBarrier();

    assert(iq_next_channel_ < channels_.size());
    channel = iq_next_channel_++;

    buf1 = *it++;
    buf2 = *it;

    if (iq_next_channel_ == channels_.size())
        nextWindow();

    return true;
}

void OverlapTDChannelizer::nextWindow(void)
{
    iq_.pop_front();
    --iq_size_;
    iq_next_channel_ = 0;
}

void OverlapTDChannelizer::ChannelState::demodulate(IQBuf &resamp_buf,
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
