// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "phy/ChannelSynthesizer.hh"

namespace py = pybind11;

template <class ChannelModulator>
ChannelSynthesizer<ChannelModulator>::ChannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                         double tx_rate,
                                                         unsigned nthreads)
  : Synthesizer(channels, tx_rate, nthreads+1)
  , enabled_(true)
  , nthreads_(nthreads)
{
    reconfigure();

    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ChannelSynthesizer::modWorker,
                                              this,
                                              i));
}

template <class ChannelModulator>
ChannelSynthesizer<ChannelModulator>::~ChannelSynthesizer()
{
    stop();
}

template <class ChannelModulator>
std::optional<size_t> ChannelSynthesizer<ChannelModulator>::getHighWaterMark(void) const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    return high_water_mark_;
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::setHighWaterMark(std::optional<size_t> high_water_mark)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    high_water_mark_ = high_water_mark;
}

template <class ChannelModulator>
bool ChannelSynthesizer<ChannelModulator>::isEnabled(void) const
{
    std::unique_lock<std::mutex> lock(mutex_);

    return enabled_;
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::enable(void)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        enabled_ = true;
    }

    producer_cv_.notify_all();
    consumer_cv_.notify_all();
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::disable(void)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        enabled_ = false;
    }

    producer_cv_.notify_all();
    consumer_cv_.notify_all();
}

template <class ChannelModulator>
TXRecord ChannelSynthesizer<ChannelModulator>::try_pop(void)
{
    TXRecord txrecord;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (!enabled_)
            return txrecord;

        txrecord = std::move(txrecord_);
    }

    producer_cv_.notify_all();

    return txrecord;
}

template <class ChannelModulator>
TXRecord ChannelSynthesizer<ChannelModulator>::pop(void)
{
    TXRecord txrecord;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        consumer_cv_.wait(lock, [&]{ return !enabled_ || txrecord_.nsamples > 0; });

        if (!enabled_ || txrecord_.nsamples == 0)
            return txrecord;

        txrecord = std::move(txrecord_);
    }

    producer_cv_.notify_all();

    return txrecord;
}

template <class ChannelModulator>
TXRecord ChannelSynthesizer<ChannelModulator>::pop_for(const std::chrono::duration<double>& rel_time)
{
    TXRecord txrecord;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        consumer_cv_.wait_for(lock, rel_time, [&]{ return !enabled_ || txrecord_.nsamples > 0; });

        if (!enabled_ || txrecord_.nsamples == 0)
            return txrecord;

        txrecord = std::move(txrecord_);
    }

    producer_cv_.notify_all();

    return txrecord;
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::push_slot(const WallClock::time_point& when, size_t slot, ssize_t prev_oversample)
{
    std::chrono::duration<double> slot_size;
    std::chrono::duration<double> guard_size;

    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!schedule_.canTransmitInSlot(slot)) {
            lock.unlock();

            std::unique_lock<std::mutex> lock(queue_mutex_);

            high_water_mark_ = 0;
        }

        slot_size = schedule_.getSlotSize();
        guard_size = schedule_.getGuardSize();
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);

    slot_ = slot;
    slot_deadline_ = when;

    txrecord_.timestamp = WallClock::to_mono_time(when);
    txrecord_.delay = prev_oversample;

    if (schedule_.mayOverfill(*chanidx_, *slot_))
        high_water_mark_ = tx_rate_*slot_size.count() - prev_oversample;
    else
        high_water_mark_ = tx_rate_*(slot_size - guard_size).count() - prev_oversample;

    producer_cv_.notify_all();
}

template <class ChannelModulator>
TXSlot ChannelSynthesizer<ChannelModulator>::pop_slot(void)
{
    TXSlot                        slot;
    std::chrono::duration<double> slot_size;
    bool                          continued;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto                   nslots = schedule_.nslots();

        slot_size = schedule_.getSlotSize();
        // This slot is continue if we can transmit in the next slot
        continued = nslots == 0 ? false : schedule_[*chanidx_][(*slot_ + 1) % nslots];
    }

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        slot.deadline = slot_deadline_;
        slot.nexcess = txrecord_.nsamples - (tx_rate_*slot_size.count() - txrecord_.delay);
        slot.continued = continued;
        slot.txrecord = std::move(txrecord_);

        slot_.reset();
        high_water_mark_ = 0;
    }

    return slot;
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::stop(void)
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

template <class ChannelModulator>
bool ChannelSynthesizer<ChannelModulator>::push(std::unique_ptr<ModPacket>&& mpkt)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (!can_push(mpkt->nsamples))
            return false;

        mpkt->start = txrecord_.nsamples;

        txrecord_.nsamples += mpkt->nsamples;
        txrecord_.iqbufs.push_back(std::move(mpkt->samples));
        txrecord_.mpkts.push_back(std::move(mpkt));
    }

    consumer_cv_.notify_one();

    return true;
}

template <class ChannelModulator>
bool ChannelSynthesizer<ChannelModulator>::can_push(size_t nsamples) const
{
    if (slot_) {
        return txrecord_.nsamples + nsamples < *high_water_mark_
            || (schedule_.mayOverfill(*chanidx_, *slot_) && txrecord_.nsamples < *high_water_mark_);
    } else
        return !high_water_mark_ || txrecord_.nsamples < *high_water_mark_;
}

template <class ChannelModulator>
bool ChannelSynthesizer<ChannelModulator>::wait_until_can_push(void)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    producer_cv_.wait(lock, [&]{ return needs_sync() || can_push(1); });

    return can_push(1);
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we don't have a channel, sleep
            if (!chanidx_ || *chanidx_ >= channels_.size()) {
                sleep_until_state_change();
                continue;
            } else {
                // Otherwise, create a modulator for the channel
                mod = std::make_unique<ChannelModulator>(channels_[*chanidx_],
                                                         *chanidx_,
                                                         tx_rate_);
            }
        }

        // Wait until we can push
        if (!wait_until_can_push())
            continue;

        // Get a packet to modulate. We may already have a packet if the last
        // push failed.
        if (!pkt) {
            if (!sink.pull(pkt))
                continue;
        }

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
        float                      g = channels_[*chanidx_].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

        mod->modulate(std::move(pkt), g, *mpkt);

        // If we didn't successfully push the packet, save the packet and try
        // again next time
        if (!push(std::move(mpkt))) {
            if (max_samples_ && mpkt->nsamples >= *max_samples_)
                logPHY(LOGWARNING, "Modulated packet is larger than slot!");
            else
                pkt = std::move(mpkt->pkt);
        }
    }
}


template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::wake_dependents(void)
{
    // Wake threads waiting on queue
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        producer_cv_.notify_all();
        consumer_cv_.notify_all();
    }

    Synthesizer::wake_dependents();
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::reconfigure(void)
{
    Synthesizer::reconfigure();

    // Use the channel that has the most available slots
    chanidx_.reset();

    if (schedule_.nchannels() > 0) {
        std::vector<int> count(schedule_.nchannels());

        for (size_t chan = 0; chan < schedule_.nchannels(); ++chan) {
            for (size_t slot = 0; slot < schedule_.nslots(); ++slot) {
                if (schedule_[chan][slot])
                    count[chan]++;
            }
        }

        auto max_idx = std::distance(count.begin(), std::max_element(count.begin(), count.end()));

        if (count[max_idx] > 0)
            chanidx_ = max_idx;
    }

    // Determine maximum number of samples in a packet
    if (schedule_.isFDMA())
        max_samples_ = std::nullopt;
    else
        max_samples_ = (schedule_.getSlotSize() - schedule_.getGuardSize()).count()*tx_rate_;
}
