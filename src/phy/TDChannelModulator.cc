// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/TDChannelModulator.hh"

void TDChannelModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                  float g,
                                  ModPacket &mpkt)
{
    const float g_effective = pkt->g*g;

    // Upsample if needed
    if (resampler_.getTheta() != 0.0 || resampler_.getRate() != 1.0) {
        // Modulate the packet, but don't paply gain yet. We will apply gain
        // when we resample.
        mod_->modulate(std::move(pkt), 1.0f, mpkt);

        // Get samples from ModPacket
        auto iqbuf = std::move(mpkt.samples);

        // Compensate for delay
        size_t         delay = ceil(resampler_.getDelay());
        const unsigned I =  resampler_.getInterpolationRate();
        const unsigned D =  resampler_.getDecimationRate();

        if (delay != 0)
            iqbuf->append(delay/I);

        // Allocate buffer for upsampled signal
        auto     iqbuf_up = std::make_shared<IQBuf>(resampler_.neededOut(iqbuf->size()));
        unsigned nsamples;

        // Reset resampler state
        resampler_.reset();

        // Resample signal
        nsamples = resampler_.resampleMixUp(iqbuf->data(),
                                            iqbuf->size(),
                                            g_effective,
                                            iqbuf_up->data());
        assert(nsamples <= iqbuf_up->size());

        // Resize output buffer
        iqbuf_up->resize(nsamples);

        // Indicate delay
        iqbuf_up->delay = delay/D;

        // Put samples back into ModPacket
        mpkt.offset = iqbuf_up->delay;
        mpkt.nsamples = iqbuf_up->size() - iqbuf_up->delay;
        mpkt.samples = std::move(iqbuf_up);
    } else
        // Modulate packet and apply gain
        mod_->modulate(std::move(pkt), g_effective, mpkt);

    // Set channel
    mpkt.chanidx = chanidx_;
    mpkt.channel = channel_.channel;
}
