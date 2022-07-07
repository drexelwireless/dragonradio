// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "ParallelChannelSynthesizer.hh"

#include <pybind11/pybind11.h>

namespace py = pybind11;

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::ParallelChannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                                         double tx_rate,
                                                                         size_t nthreads)
  : ChannelSynthesizer(channels, tx_rate)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ParallelChannelSynthesizer::modWorker,
                                              this,
                                              i));

    reconfigure();
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

    done_ = true;

    queue_.disable();

    wake_cond_.notify_all();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::reconfigure(void)
{
    // Determine channel index
    std::optional<size_t> chanidx;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t chan = 0; chan < schedule_.size(); ++chan) {
            if (schedule_.canTransmitOnChannel(chan)) {
                chanidx = chan;
                break;
            }
        }
    }

    chanidx_ = chanidx;

    // Tell workers we are reconfiguring
    reconfigure_.store(true, std::memory_order_release);

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }

    // Disable the modulated packet queue
    queue_.disable();

    // Disable the sink
    sink.disable();

    // Wait for workers to be ready for reconfiguration
    reconfigure_sync_.wait();

    // Re-enable the modulated packet queue
    queue_.enable();

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();

    // Re-enable the sink
    sink.enable();
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::vector<PHYChannel>           channels;
    std::optional<size_t>             chanidx;
    double                            tx_rate = tx_rate_;
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;
    std::unique_ptr<ModPacket>        mpkt;

    while (!done_) {
        // Wait for room
        while (!queue_.wait_until_room() && !done_)
            ;

        if (done_)
            break;

        // Reconfigure if necessary
        if (reconfigure_.load(std::memory_order_acquire)) {
            // Wait for reconfiguration to start
            reconfigure_sync_.wait();

            // Make local copies to ensure thread safety
            channels = channels_;
            chanidx = chanidx_;
            tx_rate = tx_rate_;

            // Wait for reconfiguration to finish
            reconfigure_sync_.wait();

            // If we have no channels, sleep
            if (channels.size() == 0 || !chanidx) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return done_ || reconfigure_.load(std::memory_order_acquire); });

                continue;
            } else {
                // Reconfigure the modulator
                mod = std::make_unique<ChannelModulator>(channels[*chanidx],
                                                         0,
                                                         tx_rate);
            }
        }

        // Get a packet to modulate
        if (!pkt) {
            if (!sink.pull(pkt))
                continue;
        }

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
        float                      g = channels[*chanidx].phy->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

        mod->modulate(std::move(pkt), g, *mpkt);

        // Add the packet to the queue
        queue_.push(std::move(mpkt));
    }
}
