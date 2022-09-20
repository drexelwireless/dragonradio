// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDCHANNELMODULATOR_H_
#define FDCHANNELMODULATOR_H_

#include "dsp/FDResampler.hh"
#include "phy/Synthesizer.hh"

/** @brief Channel state for frequency-domain modulation */
class FDChannelModulator : public ChannelModulator {
public:
    using C = std::complex<float>;

    using Resampler = dragonradio::signal::FDResampler<C>;

    static constexpr auto P = Resampler::P;
    static constexpr auto N = Resampler::N;

    FDChannelModulator(const PHYChannel &channel,
                       unsigned chanidx,
                       double tx_rate)
      : ChannelModulator(channel, chanidx, tx_rate)
      , resampler_(channel.I,
                   channel.D,
                   channel.phy->getTXOversampleFactor(),
                   channel.channel.fc/tx_rate,
                   channel.taps)
    {
        resampler_.setExact(true);
    }

    FDChannelModulator() = delete;

    virtual ~FDChannelModulator() = default;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  float g,
                  ModPacket &mpkt) override final;

protected:
    /** @brief Frequency domain resampler */
    Resampler resampler_;
};

#endif /* FDCHANNELMODULATOR_H_ */
