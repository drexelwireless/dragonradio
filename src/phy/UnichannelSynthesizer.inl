// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "UnichannelSynthesizer.hh"

#include <pybind11/pybind11.h>

#include "Logger.hh"

namespace py = pybind11;

template <class ChannelModulator>
UnichannelSynthesizer<ChannelModulator>::UnichannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                               double tx_rate,
                                                               size_t nthreads)
  : SlotSynthesizer(channels, tx_rate, nthreads+1)
{
    reconfigure();

    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&UnichannelSynthesizer::modWorker,
                                              this,
                                              i));
}

template <class ChannelModulator>
UnichannelSynthesizer<ChannelModulator>::~UnichannelSynthesizer()
{
    stop();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    if (modify([&](){ done_ = true; })) {
        // Join on all threads
        for (size_t i = 0; i < mod_threads_.size(); ++i) {
            if (mod_threads_[i].joinable())
                mod_threads_[i].join();
        }
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modulate(const std::shared_ptr<Slot> &slot)
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    curslot_ = slot;

    curslot_cond_.notify_all();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator>  mod;
    std::shared_ptr<Slot>              prev_slot;
    std::shared_ptr<Slot>              slot;
    std::vector<size_t>                slot_chanidx; // TX channel for each slot
    size_t                             chanidx = 0;  // Index of TX channel
    std::shared_ptr<NetPacket>         pkt;

    for (;;) {
        // Wait for the next slot
        {
            std::unique_lock<std::mutex> lock(curslot_mutex_);

            curslot_cond_.wait(lock, [&]{ return needs_sync() || curslot_ != prev_slot; });

            slot = curslot_;
        }

        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we have no schedule or channels, yield and try again
            if (schedule_.nchannels() == 0 || channels_.size() == 0) {
                std::this_thread::yield();
                continue;
            }

            // Cache which channel we use in each slot
            size_t nslots = schedule_[0].size();

            slot_chanidx.resize(nslots);

            for (size_t slot = 0; slot < nslots; ++slot)
                schedule_.firstChannelIdx(slot, slot_chanidx[slot]);

            // We need to update the modulator
            mod.release();
        }

        // If we don't have a slot, try again
        if (!slot)
            continue;

        // Skip illegal slot indices
        if (slot->slotidx >= slot_chanidx.size()) {
            logPHY(LOGDEBUG, "Bad slot index");
            continue;
        }

        if (!mod || slot_chanidx[slot->slotidx] != chanidx) {
            // Update our channel index
            chanidx = slot_chanidx[slot->slotidx];

            // Reconfigure the modulator
            mod = std::make_unique<ChannelModulator>(channels_[chanidx],
                                                     chanidx,
                                                     tx_rate_);
        }

        // We can overfill if we are allowed to transmit on the same channel in
        // the next slot in the schedule
        const Schedule::slot_type &slots = schedule_[chanidx];

        // Determine maximum number of samples in this slot
        bool overfill = getSuperslots() && slots[(slot->slotidx + 1) % slots.size()];

        if (overfill) {
            std::lock_guard<std::mutex> lock(slot->mutex);

            slot->max_samples = slot->full_slot_samples;
        }

        // Modulate packets for the current slot
        while (!needs_sync()) {
            // Get a packet to modulate
            if (!pkt) {
                if (!sink.pull(pkt))
                    continue;
            }

            // If the slot is closed, bail.
            if (slot->closed.load(std::memory_order_relaxed))
                break;

            // If this is a timestamped packet, timestamp it. In any case,
            // modulate it.
            std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
            bool                       pushed;

            // Modulate the packet
            float g = channels_[chanidx].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

            mod->modulate(std::move(pkt), g, *mpkt);

            {
                std::lock_guard<std::mutex> lock(slot->mutex);

                pushed = slot->push(mpkt, overfill);
            }

            // If we didn't successfully push the packet, try again next time
            if (!pushed) {
                if (mpkt->nsamples > slot->max_samples)
                    logPHY(LOGWARNING, "Modulated packet is larger than slot!");
                else
                    pkt = std::move(mpkt->pkt);
            }
        }

        // Remember previous slot so we can wait for a new slot before
        // attempting to modulate anything
        prev_slot = std::move(slot);
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::wake_dependents()
{
    SlotSynthesizer::wake_dependents();

    // Wake all modulation threads
    {
        std::unique_lock<std::mutex> lock(curslot_mutex_);

        curslot_cond_.notify_all();
    }
}
