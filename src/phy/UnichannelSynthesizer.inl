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
  : SlotSynthesizer(channels, tx_rate)
  , done_(false)
  , mod_reconfigure_(nthreads)
{
    for (size_t i = 0; i < nthreads; ++i) {
        mod_reconfigure_[i].store(true, std::memory_order_release);
        mod_threads_.emplace_back(std::thread(&UnichannelSynthesizer::modWorker,
                                              this,
                                              std::ref(mod_reconfigure_[i]),
                                              i));
    }
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

    done_ = true;

    curslot_cond_.notify_all();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::reconfigure(void)
{
    for (auto &flag : mod_reconfigure_)
        flag.store(true, std::memory_order_release);

    // Disable and re-enable the sink
    sink.disable();
    sink.enable();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modulate(const std::shared_ptr<Slot> &slot)
{
    std::unique_lock<std::mutex> lock(curslot_mutex_);

    curslot_ = slot;

    curslot_cond_.notify_all();
}

template <class ChannelModulator>
void UnichannelSynthesizer<ChannelModulator>::modWorker(std::atomic<bool> &reconfig, unsigned tid)
{
    std::vector<PHYChannel>            channels;
    Schedule                           schedule;
    double                             tx_rate = tx_rate_;
    std::unique_ptr<ChannelModulator>  mod;
    std::shared_ptr<Slot>              prev_slot;
    std::shared_ptr<Slot>              slot;
    std::vector<size_t>                slot_chanidx; // TX channel for each slot
    size_t                             chanidx = 0;  // Index of TX channel
    std::shared_ptr<NetPacket>         pkt;

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
            mod = std::make_unique<ChannelModulator>(channels[chanidx],
                                                     chanidx,
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
