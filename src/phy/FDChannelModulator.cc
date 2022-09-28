// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/FDChannelModulator.hh"

void FDChannelModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                  float g,
                                  ModPacket &mpkt)
{
    const auto  Nrot = upsampler_.Nrot;
    const float g_effective = pkt->g*g;

    // Interpolate if needed
    if (Nrot != 0 || rate_ != 1.0) {
        // Modulate the packet, but don't paply gain yet. We will apply gain
        // when we resample.
        mod_->modulate(std::move(pkt), 1.0f, mpkt);

        // Perform overlap-save on modulated signal to upsample it.
        auto   iqbuf = std::move(mpkt.samples);
        auto   iqbuf_up = std::make_shared<IQBuf>(upsampler_.neededOut(iqbuf->size()));
        size_t nsamples;

        nsamples = upsampler_.resample(iqbuf->data(),
                                       iqbuf->size(),
                                       iqbuf_up->data(),
                                       g_effective);

        iqbuf_up->resize(nsamples);

        // Put samples back into ModPacket
        mpkt.offset = 0;
        mpkt.nsamples = iqbuf_up->size();
        mpkt.samples = std::move(iqbuf_up);
    } else {
        // Modulate packet and apply gain
        auto pkt_g = pkt->g*g;

        mod_->modulate(std::move(pkt), pkt_g, mpkt);
    }

    // Set channel
    mpkt.chanidx = chanidx_;
    mpkt.channel = channel_.channel;
}
