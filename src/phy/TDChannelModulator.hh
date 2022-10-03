// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDCHANNELMODULATOR_H_
#define TDCHANNELMODULATOR_H_

#include "dsp/Polyphase.hh"
#include "phy/Synthesizer.hh"

/** @brief Channel state for time-domain modulation */
class TDChannelModulator : public ChannelModulator {
public:
    using C = std::complex<float>;

    using Upsampler = dragonradio::signal::pfb::MixingRationalResampler<C,C>;

    TDChannelModulator(const PHYChannel &channel,
                       unsigned chanidx,
                       double tx_rate)
      : ChannelModulator(channel, chanidx, tx_rate)
      , upsampler_(channel.channel.bw == 0.0 ? 1.0 : tx_rate/(channel.phy->getMinTXRateOversample()*channel.channel.bw),
                   2*M_PI*channel.channel.fc/tx_rate,
                   channel.taps)
    {
    }

    TDChannelModulator() = delete;

    virtual ~TDChannelModulator() = default;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  float g,
                  ModPacket &mpkt) override final;
protected:
    /** @brief Time domain upsampler */
    Upsampler upsampler_;
};

#endif /* TDCHANNELMODULATOR_H_ */
