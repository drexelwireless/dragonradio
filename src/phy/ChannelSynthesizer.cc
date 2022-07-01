// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/ChannelSynthesizer.hh"

void ChannelSynthesizer::wake_dependents(void)
{
    // Disable the modulated packet queue
    queue_.disable();

    // Disable the sink
    sink.disable();
}

void ChannelSynthesizer::reconfigure(void)
{
    // Determine channel index
    std::optional<size_t> chanidx;

    for (size_t chan = 0; chan < schedule_.nchannels(); ++chan) {
        if (schedule_.canTransmitOnChannel(chan)) {
            chanidx = chan;
            break;
        }
    }

    chanidx_ = chanidx;

    // Re-enable the modulated packet queue
    queue_.enable();

    // Re-enable the sink
    sink.enable();
}
