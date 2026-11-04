// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "UnichannelSynthesizer.hh"
#include "util/WorkMeter.hh"
#include "util/threads.hh"

#include <pybind11/pybind11.h>

namespace py = pybind11;

template <class ChannelModulator>
UnichannelSynthesizer<ChannelModulator>::UnichannelSynthesizer(std::shared_ptr<PHY> phy,
                                                               double tx_rate,
                                                               const Channels &channels,
                                                               size_t nthreads)
  : SlotSynthesizer(phy, tx_rate, channels)
  , done_(false)
  , mod_reconfigure_(nthreads)
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(true, std::memory_order_release);
        mod_threads_.emplace_back(std::thread(&UnichannelSynthesizer::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i]),
                                              i));
        setThreadName(mod_threads_.back(), "dr-mod", i);
    }

    kicker_.kick = [this]{ sink.kick(); };
    kicker_.thread = std::thread(&Kicker::run, &kicker_);
    setThreadName(kicker_.thread, "dr-mod-kick");
}

template <class ChannelModulator>
UnichannelSynthesizer<ChannelModulator>::~UnichannelSynthesizer()
{
    stop();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modulate(const std::shared_ptr<Slot> &slot)
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    curslot_ = slot;

    curslot_cond_.notify_all();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::reconfigure(void)
{
    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_release);

    // Kick the sink
    sink.kick();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;

    curslot_cond_.notify_all();

    kicker_.shutdown();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modWorker(std::atomic<bool> &reconfig, unsigned tid)
{
    static std::atomic<unsigned> next_meter{0};
    WorkMeter meter("dr-mod", next_meter++);
    Channels                           channels;
    Schedule                           schedule;
    double                             tx_rate = tx_rate_;
    std::unique_ptr<ChannelModulator>  mod;
    std::shared_ptr<Slot>              prev_slot;
    std::shared_ptr<Slot>              slot;
    std::vector<size_t>                slot_chanidx; // TX channel for each slot
    size_t                             chanidx = 0;  // Index of TX channel
    std::shared_ptr<NetPacket>         pkt;
    // Packets pulled while looking for an OFDMA burst partner, in order.
    std::deque<std::shared_ptr<NetPacket>> held;
    const unsigned                     max_users = phy_->getMaxBurstUsers();
    constexpr size_t                   kMaxHeld = 4;

    while (!done_) {
        // Wait for the next slot
        {
            std::unique_lock<std::mutex> lock(curslot_mutex_);

            curslot_cond_.wait(lock, [&]{ return done_ || curslot_ != prev_slot; });

            slot = curslot_;
        }

        // Exit now if we're done
        if (done_)
            break;

        // Reconfigure if necessary
        if (reconfig.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(mutex_);

            // Make local copies to ensure thread safety
            channels = channels_;
            schedule = schedule_;
            tx_rate = tx_rate_;

            // If we have no schedule or channels, yield and try again
            if (schedule.size() == 0 || channels.size() == 0) {
                reconfig.store(false, std::memory_order_relaxed);
                std::this_thread::yield();
                continue;
            }

            // Cache which channel we use in each slot
            size_t nslots = schedule[0].size();

            slot_chanidx.resize(nslots);

            for (size_t slot = 0; slot < nslots; ++slot)
                schedule.firstChannelIdx(slot, slot_chanidx[slot]);

            // We need to update the modulator
            mod.release();

            reconfig.store(false, std::memory_order_relaxed);
        }

        // Skip illegal slot indices
        if (slot->slotidx >= slot_chanidx.size()) {
            logPHY(LOGDEBUG, "Bad slot index");
            continue;
        }

        if (!mod || slot_chanidx[slot->slotidx] != chanidx) {
            // Update our channel index
            chanidx = slot_chanidx[slot->slotidx];

            // Reconfigure the modulator
            mod = std::make_unique<ChannelModulator>(*phy_,
                                                     chanidx,
                                                     channels[chanidx].first,
                                                     channels[chanidx].second,
                                                     tx_rate);
        }

        // We can overfill if we are allowed to transmit on the same channel in
        // the next slot in the schedule
        const Schedule::slot_type &slots = schedule[chanidx];

        // Determine maximum number of samples in this slot
        bool overfill = getSuperslots() && slots[(slot->slotidx + 1) % slots.size()];

        if (overfill) {
            std::lock_guard<std::mutex> lock(slot->mutex);

            slot->max_samples = slot->full_slot_samples;
        }

        // Modulate packets for the current slot
        while (!done_) {
            // Get a packet to modulate
            if (!pkt && !held.empty()) {
                pkt = std::move(held.front());
                held.pop_front();
            }
            if (!pkt) {
                if (!sink.pull(pkt))
                    continue;
            }

            // If the slot is closed, bail.
            if (slot->closed.load(std::memory_order_relaxed))
                break;

            // OFDMA: a second unicast packet for another destination shares
            // the burst. Look among held packets, then wait at most the burst
            // wait for one; anything unsuitable is held, in order.
            std::vector<std::shared_ptr<NetPacket>> burst;

            if (max_users > 1 && pkt->hdr.nexthop != kNodeBroadcast) {
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

            // If this is a timestamped packet, timestamp it. In any case,
            // modulate it.
            std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
            bool                       pushed;

            // Modulate the packet
            float g = phy_->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

            {
                WorkMeter::Scope work(meter);

                if (burst.empty()) {
                    mod->modulate(std::move(pkt), g, *mpkt);
                } else {
                    burst.insert(burst.begin(), std::move(pkt));
                    mod->modulateBurst(std::move(burst), g, *mpkt);
                }
            }

            {
                std::lock_guard<std::mutex> lock(slot->mutex);

                pushed = slot->push(mpkt, overfill);
            }

            // If we didn't successfully push the packet, it cannot fit in
            // this slot (max_samples is fixed and other threads only add
            // samples): we are done with this slot and will add the packet to
            // the next one, as MultichannelSynthesizer does. Retrying here
            // re-modulated the same packet in a tight loop until the slot
            // closed (P4; tests/fullstack/test_synth_slot.cc).
            if (!pushed) {
                pkt = std::move(mpkt->pkt);
                for (auto p = mpkt->partners.rbegin(); p != mpkt->partners.rend(); ++p)
                    held.push_front(std::move(*p));
                break;
            }
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::Kicker::run(void)
{
    std::unique_lock<std::mutex> lock(m);

    while (!stop) {
        if (!deadline) {
            cv.wait(lock);
            continue;
        }
        if (cv.wait_until(lock, *deadline) == std::cv_status::timeout && deadline
            && std::chrono::steady_clock::now() >= *deadline) {
            deadline.reset();
            lock.unlock();
            kick();
            lock.lock();
        }
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::Kicker::arm(double sec)
{
    std::lock_guard<std::mutex> lock(m);

    deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(sec));
    cv.notify_all();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::Kicker::cancel(void)
{
    std::lock_guard<std::mutex> lock(m);

    deadline.reset();
    cv.notify_all();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::Kicker::shutdown(void)
{
    {
        std::lock_guard<std::mutex> lock(m);

        stop = true;
        cv.notify_all();
    }
    if (thread.joinable())
        thread.join();
}

template <class ChannelModulator>
bool UnichannelSynthesizer<ChannelModulator>::pullWithin(std::shared_ptr<NetPacket> &pkt, double sec)
{
    // A kick makes the blocked pull return false once. If the deadline
    // passes just after a packet arrived, the next pull may return false
    // spuriously, which the worker treats as no packet.
    kicker_.arm(sec);
    bool ok = sink.pull(pkt);
    kicker_.cancel();
    return ok && pkt;
}
