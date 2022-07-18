// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "ParallelChannelSynthesizer.hh"

#include <pybind11/pybind11.h>

namespace py = pybind11;

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::ParallelChannelSynthesizer(const std::vector<PHYChannel> &channels,
                                                                         double tx_rate,
                                                                         size_t nthreads)
  : ChannelSynthesizer(channels, tx_rate, nthreads)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ParallelChannelSynthesizer::modWorker,
                                              this,
                                              i));

    modify([&]() { reconfigure(); });
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;
    std::unique_ptr<ModPacket>        mpkt;

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;

            // If we don't have a channel, sleep
            if (!chanidx_) {
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
        if (!push(std::move(mpkt)))
            pkt = std::move(mpkt->pkt);
    }
}
