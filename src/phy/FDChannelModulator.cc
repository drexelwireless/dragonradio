// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/FDChannelModulator.hh"

void FDChannelModulator::modulate(std::shared_ptr<NetPacket> pkt,
                                  float g,
                                  ModPacket &mpkt)
{
    const auto     Nrot = upsampler_.Nrot;
    const auto     X = upsampler_.X;
    const auto     L = upsampler_.L;
    const auto     I = upsampler_.I;
    const unsigned Li = X*L/I; // Number of samples consumed per input block
    const float    g_effective = pkt->g*g;

    // Interpolate if needed
    if (Nrot != 0 || rate_ != 1.0) {
        // Modulate the packet, but don't paply gain yet. We will apply gain
        // when we resample.
        mod_->modulate(std::move(pkt), 1.0f, mpkt);

        // Perform overlap-save on modulated signal to upsample it.
        //
        // Each block of Li input samples results in a block of N output
        // frequency domain samples. We add Li - 1 to round up.
        //
        // We zero the frequency-domain buffer because we only copy our signal
        // into the frequency bins it occupies in the upsampled frequency space
        // while leaving the other binds untouched.
        auto   iqbuf = std::move(mpkt.samples);
        auto   fdbuf = std::make_shared<IQBuf>(N*((iqbuf->size() + Li - 1)/Li));
        size_t nsamples = 0;
        size_t fdnsamples = 0;

        fdbuf->zero();
        upsampler_.reset();
        upsampler_.upsample(iqbuf->data(),
                            iqbuf->size(),
                            fdbuf->data(),
                            g_effective,
                            true,
                            nsamples,
                            I*iqbuf->size()/X,
                            fdnsamples);
        fdbuf->resize(fdnsamples);

        // Now convert upsampled signal back to time domain
        auto iqbuf_up = std::make_shared<IQBuf>(L*((fdbuf->size() + N - 1)/N));

        timedomain_.toTimeDomain(fdbuf->data(), fdbuf->size(), iqbuf_up->data());

        iqbuf_up->resize(I*iqbuf->size()/X);

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
    mpkt.channel = channel_;
}
