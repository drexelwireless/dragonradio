// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <pybind11/pybind11.h>

#include "phy/ChannelSynthesizer.hh"

namespace py = pybind11;

ChannelSynthesizer::~ChannelSynthesizer()
{
    stop();
}

void ChannelSynthesizer::stop(void)
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

void ChannelSynthesizer::wake_dependents(void)
{
    Synthesizer::wake_dependents();

    // Wake threads waiting on queue
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        producer_cv_.notify_all();
        consumer_cv_.notify_all();
    }

    Synthesizer::wake_dependents();
}

void ChannelSynthesizer::reconfigure(void)
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
}
