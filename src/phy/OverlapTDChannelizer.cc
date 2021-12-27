// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <functional>

#include "logging.hh"
#include "phy/PHY.hh"
#include "phy/OverlapTDChannelizer.hh"

using namespace std::placeholders;

OverlapTDChannelizer::OverlapTDChannelizer(const std::vector<PHYChannel> &channels,
                                           double rx_rate,
                                           unsigned int nthreads)
  : Channelizer(channels, rx_rate)
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
    {
        std::lock_guard<std::mutex> lock(mutex_);

        prev_demod_samps_ = prev_demod_*rx_rate_;
        cur_demod_samps_ = cur_demod_*rx_rate_;
    }

    {
        std::lock_guard<std::mutex> lock(iq_mutex_);

        if (iq_next_channel_ >= channels_.size())
            nextWindow();
    }

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
    std::vector<std::unique_ptr<OverlapTDChannelDemodulator>> demods;
    OverlapTDChannelDemodulator                               *demod;
    RadioPacketQueue::barrier                                 b;
    unsigned                                                  chanidx;
    std::shared_ptr<IQBuf>                                    buf1;
    std::shared_ptr<IQBuf>                                    buf2;
    bool                                                      received;

    auto callback = [&] (const std::shared_ptr<RadioPacket> &pkt) {
        received = true;
        if (pkt) {
            pkt->chanidx = chanidx;
            pkt->channel = channels_[chanidx].channel;

            if (enforce_ordering_)
                radio_q_.push(b, pkt);
            else
                source.push(pkt);
        }
    };

    while (!done_) {
        if (!pop(b, chanidx, buf1, buf2))
            break;

        received = false;

        // Calculate how many samples we want to demodulate from the tail end of
        // the previous slot
        size_t buf1_nsamples = buf1->oversample + prev_demod_samps_;

        // This can happen when the user has set a large demodulation overlap
        if (buf1_nsamples > buf1->size())
            buf1_nsamples = buf1->size();

        // Calculate offset into buf1 at which we begin demodulation
        size_t buf1_off = buf1->size() - buf1_nsamples;

        // Handle reconfiguration
        if (reconfig.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(mutex_);

            demods.resize(channels_.size());

            for (unsigned i = 0; i < channels_.size(); i++) {
                demods[i] = std::make_unique<OverlapTDChannelDemodulator>(channels_[i],
                                                                          rx_rate_);
                demods[i]->setCallback(callback);
            }

            reconfig.store(false, std::memory_order_relaxed);
        }

        demod = demods[chanidx].get();

        // Reset the state of the demodulator
        demod->reset();

        // Demodulate the last part of the guard interval of the previous slots
        demod->timestamp(*buf1->timestamp,
                         buf1->snapshot_off,
                         buf1_off);

        demod->demodulate(buf1->data() + buf1_off,
                          buf1_nsamples);

        // Wait for the second buffer to start to fill. If demodulation is very
        // fast, it is possible for us to finish demodulating the first buffer
        // before the second begins to fill! This actually happens with OFDM.
        buf2->waitToStartFilling();

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
            std::optional<ssize_t> snapshot_off;

            if (buf2->snapshot_off)
                snapshot_off = buf2->snapshot_off;
            else if (buf1->snapshot_off)
                snapshot_off = *buf1->snapshot_off + buf1->size();

            demod->timestamp(*buf2->timestamp,
                             snapshot_off,
                             0);

            nwanted = cur_demod_samps_ - buf2->undersample;

            for (;;) {
                complete = buf2->complete.load(std::memory_order_acquire);
                n = std::min(buf2->nsamples.load(std::memory_order_acquire) - ndemodulated, nwanted);

                if (n != 0) {
                    demod->demodulate(&(*buf2)[ndemodulated],
                                      n);

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
            logger_->logSlot(buf1);
            logger_->logSlot(buf2);
        }
    }
}

void OverlapTDChannelizer::netWorker(void)
{
    std::shared_ptr<RadioPacket> pkt;

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
            logPHY(LOGWARNING, "Large demodulation queue: size=%lu",
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
    if (iq_size_ > 0) {
        iq_.pop_front();
        --iq_size_;
        iq_next_channel_ = 0;
    }
}

void OverlapTDChannelizer::OverlapTDChannelDemodulator::reset(void)
{
    resamp_.reset();
    demod_->reset(channel_.channel);
}


void OverlapTDChannelizer::OverlapTDChannelDemodulator::timestamp(const MonoClock::time_point &timestamp,
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

void OverlapTDChannelizer::OverlapTDChannelDemodulator::demodulate(const std::complex<float>* data,
                                                                   size_t count)
{
    if (fshift_ != 0.0 || rate_ != 1.0) {
        // Resample. Note that we can't very well mix without a frequency shift,
        // so we are guaranteed that the resampler's rate is not 1 here.
        unsigned nw;

        resamp_buf_.resize(resamp_.neededOut(count));
        nw = resamp_.resampleMixDown(data, count, resamp_buf_.data());
        resamp_buf_.resize(nw);

        // Demodulate resampled data.
        demod_->demodulate(resamp_buf_.data(), resamp_buf_.size());
    } else
        demod_->demodulate(data, count);
}
