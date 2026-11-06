// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <algorithm>

#include <pybind11/pybind11.h>

#include "logging.hh"
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
    ++slot_gen_;

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
        ++slot_gen_;
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

    // A worker may be waiting for a burst partner
    kicker_.shutdown();

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
bool ChannelSynthesizer<ChannelModulator>::push(std::unique_ptr<ModPacket>&& mpkt,
                                                std::optional<uint64_t> *full_slot,
                                                bool *empty_slot)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (!can_push(mpkt->nsamples)) {
            if (full_slot && slot_)
                *full_slot = slot_gen_;
            if (empty_slot)
                *empty_slot = slot_ && txrecord_.nsamples == 0;
            return false;
        }

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
bool ChannelSynthesizer<ChannelModulator>::wait_until_can_push(std::optional<uint64_t> full_slot)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);
    auto                         ready = [&]{ return can_push(1) && (!full_slot || slot_gen_ != *full_slot); };

    producer_cv_.wait(lock, [&]{ return needs_sync() || ready(); });

    return ready();
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;
    std::optional<uint64_t>           full_slot; // Slot pkt did not fit in
    // Downlink OFDMA: packets pulled while looking for a burst partner, in
    // order, and the PHY's packets per burst.
    std::deque<std::shared_ptr<NetPacket>> held;
    unsigned                               max_users = 1;
    constexpr size_t                       kMaxHeld = 4;
    // Modulate pkt alone: its burst with a partner did not fit in a slot.
    bool                                   solo = false;

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            // The schedule may have changed (FDMA has no slots to wait for).
            full_slot.reset();

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
                max_users = channels_[*chanidx_].phy->getMaxBurstUsers();

                // One kicker serves the synthesizer, and a kick ends
                // whichever pop is blocked, so batching needs a single
                // modulation thread (the default).
                if (max_users > 1 && nthreads_ > 1) {
                    if (tid == 0)
                        logPHY(LOGWARNING, "OFDMA burst batching needs one modulation thread; %u configured: one packet per burst",
                               nthreads_);
                    max_users = 1;
                }
            }
        }

        // Wait until we can push, and if pkt did not fit in a slot, until
        // that slot is over
        if (!wait_until_can_push(full_slot))
            continue;

        full_slot.reset();

        // Get a packet to modulate. We may already have a packet if the last
        // push failed, or packets held while looking for a burst partner.
        if (!pkt && !held.empty()) {
            pkt = std::move(held.front());
            held.pop_front();
        }

        if (!pkt) {
            if (!sink.pull(pkt)) {
                // Packets may be queued that cannot leave yet (send window
                // closed). Without a pause this loop retakes the queue lock
                // continuously and starves everything that pushes to it: the
                // tap reader and control messages (2026-11-05, stalls of
                // several seconds at traffic start on hardware).
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }
        }

        // Downlink OFDMA (PHYs with getMaxBurstUsers() > 1): a second unicast
        // packet for another next hop shares the burst. Look among held
        // packets, then wait at most the burst wait for one; anything
        // unsuitable is held, in order. Other PHYs never get here.
        std::vector<std::shared_ptr<NetPacket>> burst;

        if (max_users > 1 && !solo && pkt->hdr.nexthop != kNodeBroadcast) {
            auto suitable = [&](const std::shared_ptr<NetPacket> &p) {
                return p->hdr.nexthop != kNodeBroadcast && p->hdr.nexthop != pkt->hdr.nexthop;
            };
            auto it = std::find_if(held.begin(), held.end(), suitable);

            if (it != held.end()) {
                burst.push_back(std::move(*it));
                held.erase(it);
            } else if (held.size() < kMaxHeld) {
                std::shared_ptr<NetPacket> other;

                if (pullWithin(other, getBurstWait())) {
                    if (suitable(other))
                        burst.push_back(std::move(other));
                    else
                        held.push_back(std::move(other));
                }
            }
        }

        // Modulate the packet (and its burst partner)
        std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
        float                      g = channels_[*chanidx_].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

        solo = false;

        if (burst.empty())
            mod->modulate(std::move(pkt), g, *mpkt);
        else {
            burst.insert(burst.begin(), std::move(pkt));
            mod->modulateBurst(std::move(burst), g, *mpkt);
        }

        // If we didn't successfully push the packet, save the packet and try
        // again next time. A packet that did not fit in the rest of a slot
        // cannot fit later in the same slot (the high-water mark is fixed
        // when the slot is pushed and other threads only add samples), so
        // push records the slot and we wait for the next one. Retrying at
        // once re-modulated the packet in a tight loop until the slot was
        // popped (tests/fullstack/test_synth_slot.cc; codex/ofdma-simulation
        // f0f0f3b8 fixed the same bug in UnichannelSynthesizer).
        bool empty_slot = false;

        if (!push(std::move(mpkt), &full_slot, &empty_slot)) {
            if (!mpkt->partners.empty()) {
                // A two-user burst that does not fit is split, whether it is
                // too long for any slot or only for what is left of this one:
                // a burst is about twice as long as one packet (each user
                // gets part of the tones), so the rest of a slot often holds
                // one packet but not a burst. Try the first packet alone in
                // this slot, and hold its partner for the next burst. Carrying
                // both to the next slot left about a third of most slots
                // empty (2026-11-06 OTA: 404 of 444 slots). If the packet
                // alone does not fit either, push records the full slot and
                // it waits for the next one.
                pkt = std::move(mpkt->pkt);
                for (auto p = mpkt->partners.rbegin(); p != mpkt->partners.rend(); ++p)
                    held.push_front(std::move(*p));
                solo = true;
                full_slot.reset();
            } else if (max_samples_ && mpkt->nsamples >= *max_samples_) {
                logPHY(LOGWARNING, "Modulated packet is larger than slot!");
                full_slot.reset();
            } else {
                // A packet that does not fit waits for the next slot.
                pkt = std::move(mpkt->pkt);
            }
        }
    }
}

template <class ChannelModulator>
bool ChannelSynthesizer<ChannelModulator>::pullWithin(std::shared_ptr<NetPacket> &pkt, double sec)
{
    auto q = getBurstQueue();

    // Without a queue to kick, a pull could block indefinitely: do not wait.
    if (!q || sec <= 0)
        return false;

    // A kick makes the blocked pull return false once. If the deadline
    // passes just after a packet arrived, the next pull on an empty queue may
    // return false spuriously, which the worker treats as no packet.
    kicker_.arm(sec, q);
    bool ok = sink.pull(pkt);
    kicker_.cancel();

    return ok && pkt;
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::Kicker::run(void)
{
    std::unique_lock<std::mutex> lock(m);

    while (!stop) {
        if (!deadline) {
            cv.wait(lock);
            continue;
        }

        if (cv.wait_until(lock, *deadline) == std::cv_status::timeout
            && deadline
            && std::chrono::steady_clock::now() >= *deadline) {
            auto q = queue;

            deadline.reset();
            lock.unlock();
            if (q)
                q->kick();
            lock.lock();
        }
    }
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::Kicker::arm(double sec, std::shared_ptr<Queue<std::shared_ptr<NetPacket>>> q)
{
    std::lock_guard<std::mutex> lock(m);

    if (stop)
        return;

    if (!thread.joinable())
        thread = std::thread(&Kicker::run, this);

    queue = std::move(q);
    deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(sec));
    cv.notify_all();
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::Kicker::cancel(void)
{
    std::lock_guard<std::mutex> lock(m);

    deadline.reset();
    cv.notify_all();
}

template <class ChannelModulator>
void ChannelSynthesizer<ChannelModulator>::Kicker::shutdown(void)
{
    {
        std::lock_guard<std::mutex> lock(m);

        stop = true;
        deadline.reset();
        queue.reset();
        cv.notify_all();
    }

    if (thread.joinable() && thread.get_id() != std::this_thread::get_id())
        thread.join();
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
