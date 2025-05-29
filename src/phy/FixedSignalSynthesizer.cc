// Copyright 2024 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "phy/FixedSignalSynthesizer.hh"

namespace py = pybind11;

FixedSignalSynthesizer::FixedSignalSynthesizer(const std::vector<PHYChannel> &channels,
                                               double tx_rate)
  : Synthesizer(channels, tx_rate, 2)
  , enabled_(true)
{
    reconfigure();

    mod_thread_ = std::thread(&FixedSignalSynthesizer::modWorker, this);
}

FixedSignalSynthesizer::~FixedSignalSynthesizer()
{
    stop();
}

std::optional<size_t> FixedSignalSynthesizer::getHighWaterMark(void) const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    return high_water_mark_;
}

void FixedSignalSynthesizer::setHighWaterMark(std::optional<size_t> high_water_mark)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    high_water_mark_ = high_water_mark;
}

bool FixedSignalSynthesizer::isEnabled(void) const
{
    std::unique_lock<std::mutex> lock(mutex_);

    return enabled_;
}

void FixedSignalSynthesizer::enable(void)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        enabled_ = true;
    }

    producer_cv_.notify_all();
    consumer_cv_.notify_all();
}

void FixedSignalSynthesizer::disable(void)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);

        enabled_ = false;
    }

    producer_cv_.notify_all();
    consumer_cv_.notify_all();
}

TXRecord FixedSignalSynthesizer::try_pop(void)
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

TXRecord FixedSignalSynthesizer::pop(void)
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

TXRecord FixedSignalSynthesizer::pop_for(const std::chrono::duration<double>& rel_time)
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

void FixedSignalSynthesizer::push_slot(const WallClock::time_point& when, size_t slot, ssize_t prev_oversample)
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

TXSlot FixedSignalSynthesizer::pop_slot(void)
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

void FixedSignalSynthesizer::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    // Set done flag
    if (modify([&](){ done_ = true; })) {
        // Join on "modulation" thread
        if (mod_thread_.joinable())
            mod_thread_.join();
    }
}

bool FixedSignalSynthesizer::push(const std::shared_ptr<IQBuf> &buf)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (!can_push(buf->nsamples))
            return false;

        txrecord_.nsamples += buf->size();
        txrecord_.iqbufs.push_back(buf);
    }

    consumer_cv_.notify_one();

    return true;
}

bool FixedSignalSynthesizer::can_push(size_t nsamples) const
{
    if (slot_) {
        return txrecord_.nsamples + nsamples < *high_water_mark_
            || (schedule_.mayOverfill(*chanidx_, *slot_) && txrecord_.nsamples < *high_water_mark_);
    } else
        return !high_water_mark_ || txrecord_.nsamples < *high_water_mark_;
}

bool FixedSignalSynthesizer::wait_until_can_push(void)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    producer_cv_.wait(lock, [&]{ return needs_sync() || can_push(1); });

    return can_push(1);
}

void FixedSignalSynthesizer::setSignal(const std::shared_ptr<IQBuf> &buf)
{
    modify([&](){ signal_ = buf; });
}

const std::shared_ptr<IQBuf> FixedSignalSynthesizer::getSignal(void)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return signal_;
}

void FixedSignalSynthesizer::modWorker(void)
{
    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we don't have a channel, sleep
            if (!chanidx_ || *chanidx_ >= channels_.size() || !this->signal_) {
                sleep_until_state_change();
                continue;
            }
        }

        // Wait until we can push
        if (!wait_until_can_push())
            continue;

        // Push fixed signal
        assert(this->signal_);
        push(this->signal_);
    }
}

void FixedSignalSynthesizer::wake_dependents(void)
{
    // Wake threads waiting on queue
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        producer_cv_.notify_all();
        consumer_cv_.notify_all();
    }

    Synthesizer::wake_dependents();
}

void FixedSignalSynthesizer::reconfigure(void)
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
