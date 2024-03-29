// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <functional>

#include <pybind11/pybind11.h>

namespace py = pybind11;

#include "Logger.hh"
#include "phy/PHY.hh"
#include "phy/TDChannelizer.hh"

using namespace std::placeholders;

TDChannelizer::TDChannelizer(std::shared_ptr<PHY> phy,
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

void TDChannelizer::push(const std::shared_ptr<IQBuf> &iqbuf)
{
    std::lock_guard<std::mutex> lock(demod_mutex_);
    unsigned                    nchannels = channels_.size();

    for (unsigned i = 0; i < nchannels; ++i)
        iqbufs_[i]->push(iqbuf);
}

void TDChannelizer::reconfigure(void)
{
    std::lock_guard<std::mutex> lock(demod_mutex_);

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

    for (unsigned i = 0; i < nchannels; i++) {
        if (!iqbufs_[i])
            iqbufs_[i] = std::make_unique<SafeQueue<std::shared_ptr<IQBuf>>>();

        demods_[i] = std::make_unique<TDChannelDemodulator>(*phy_,
                                                            channels_[i].first,
                                                            channels_[i].second,
                                                            rx_rate_);
    }

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();
}

void TDChannelizer::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    done_ = true;

    // Stop all IQ buffer queues
    for (unsigned int i = 0; i < iqbufs_.size(); ++i)
        iqbufs_[i]->stop();

    // Join on all threads
    wake_cond_.notify_all();

    for (unsigned int i = 0; i < demod_threads_.size(); ++i) {
        if (demod_threads_[i].joinable())
            demod_threads_[i].join();
    }
}

void TDChannelizer::demodWorker(unsigned tid)
{
    std::shared_ptr<IQBuf> prev_iqbuf;
    std::shared_ptr<IQBuf> iqbuf;
    std::optional<ssize_t> next_snapshot_off;
    bool                   received = false; // Have we received any packets?
    Channel                channel;          // Current channel being demodulated

    PHY::PacketDemodulator::callback_type callback = [&] (const std::shared_ptr<RadioPacket> &pkt) {
        received = true;
        if (pkt) {
            pkt->channel = channel;
            source.push(pkt);
        }
    };

    while (!done_) {
        // If we are reconfiguring, wait until reconfiguration is done
        if (reconfigure_.load(std::memory_order_acquire)) {
            // Wait for reconfiguration to start
            reconfigure_sync_.wait();

            // Wait for reconfiguration to finish
            reconfigure_sync_.wait();

            // If we are unneeded, sleep
            if (tid >= channels_.size()) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return done_ || reconfigure_.load(std::memory_order_acquire); });

                continue;
            }

            // Set demodulator callbacks
            for (unsigned channelidx = tid; channelidx < channels_.size(); channelidx += nthreads_)
                demods_[channelidx]->setCallback(callback);
        }

        for (unsigned channelidx = tid; channelidx < channels_.size(); channelidx += nthreads_) {
            auto &demod = *demods_[channelidx];
            auto &iqbufs = *iqbufs_[channelidx];

            // Get an IQ buffer
            if (!iqbufs.pop(iqbuf)) {
                if (done_)
                    return;

                continue;
            }

            // Wait for the buffer to start to fill.
            iqbuf->waitToStartFilling();

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
            demod.updateSeq(iqbuf->seq);

            // Timestamp the demodulated data
            demod.timestamp(*iqbuf->timestamp,
                            snapshot_off,
                            0,
                            rx_rate_);

            // Demodulate the IQ buffer
            bool   complete = false; // Is the buffer complete?
            size_t ndemodulated = 0; // How many samples we've already demodulated
            size_t n = 0;

            received = false;
            channel = channels_[channelidx].first;

            for (;;) {
                complete = iqbuf->complete.load(std::memory_order_acquire);
                n = iqbuf->nsamples.load(std::memory_order_acquire) - ndemodulated;

                if (n != 0) {
                    demod.demodulate(iqbuf->data() + ndemodulated, n);
                    ndemodulated += n;
                } else if (complete)
                    break;
            }

            // Save the snapshot offset of the next IQ buffer here if we know
            // what it will be. iqbuf's size is valid now that it has been
            // marked complete.
            if (iqbuf->snapshot_off)
                next_snapshot_off = *iqbuf->snapshot_off + iqbuf->size();
            else
                next_snapshot_off = std::nullopt;

            // If we received any packets, log both the previous and the current
            // slot. We then save the current slot in case we need to log it
            // later.
            if (logger_ && logger_->getCollectSource(Logger::kSlots)) {
                if (received) {
                    if (prev_iqbuf) {
                        logger_->logSlot(prev_iqbuf);
                        prev_iqbuf.reset();
                    }

                    logger_->logSlot(iqbuf);
                } else
                    prev_iqbuf = std::move(iqbuf);
            }
        }
    }
}

void TDChannelizer::TDChannelDemodulator::updateSeq(unsigned seq)
{
    // Reset state if we have a discontinuity or if we're not currently
    // receiving a frame
    if (seq != seq_ + 1 || !demod_->isFrameOpen())
        reset();

    // Record buffer sequence number
    seq_ = seq;
}

void TDChannelizer::TDChannelDemodulator::reset(void)
{
    resamp_.reset();
    demod_->reset(channel_);
    seq_ = 0;
}

void TDChannelizer::TDChannelDemodulator::timestamp(const MonoClock::time_point &timestamp,
                                                    std::optional<ssize_t> snapshot_off,
                                                    ssize_t offset,
                                                    float rx_rate)
{
    demod_->timestamp(timestamp,
                      snapshot_off,
                      offset,
                      delay_,
                      rate_,
                      rx_rate);
}

void TDChannelizer::TDChannelDemodulator::demodulate(const std::complex<float>* data,
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
