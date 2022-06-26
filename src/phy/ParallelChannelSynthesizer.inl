// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "ParallelChannelSynthesizer.hh"

#include <pybind11/pybind11.h>

namespace py = pybind11;

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::ParallelChannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                                         double tx_rate,
                                                                         size_t nthreads)
  : ChannelSynthesizer(channels, tx_rate, nthreads+1)
  , nthreads_(nthreads)
{
    reconfigure();

    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ParallelChannelSynthesizer::modWorker,
                                              this,
                                              i));
}

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::~ParallelChannelSynthesizer()
{
    stop();
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::stop(void)
{
    // Release the GIL in case we have Python-based demodulators
    py::gil_scoped_release gil;

    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    // Disable the queue
    queue_.disable();

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
void ParallelChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;
    std::unique_ptr<ModPacket>        mpkt;

    for (;;) {
        // Wait for room
        while (!queue_.wait_until_room() && !needs_sync())
            ;

        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we have no channels, sleep
            if (channels_.size() == 0 || !chanidx_) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return needs_sync(); });

                continue;
            } else {
                // Reconfigure the modulator
                mod = std::make_unique<ChannelModulator>(channels_[*chanidx_],
                                                         0,
                                                         tx_rate_);
            }
        }

        // Get a packet to modulate
        if (!pkt) {
            if (!sink.pull(pkt))
                continue;
        }

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
        float                      g = channels_[*chanidx_].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

        mod->modulate(std::move(pkt), g, *mpkt);

        // Add the packet to the queue
        queue_.push(std::move(mpkt));
    }
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::wake_dependents()
{
    ChannelSynthesizer::wake_dependents();

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }
}
