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

    using Resampler = dragonradio::signal::pfb::MixingRationalResampler<C,C>;

    TDChannelModulator(const PHYChannel &channel,
                       unsigned chanidx,
                       double tx_rate)
      : ChannelModulator(channel, chanidx, tx_rate)
      , resampler_(channel.I,
                   channel.D,
                   channel.channel.fc/tx_rate,
                   channel.taps)
    {
    }

    TDChannelModulator() = delete;

    virtual ~TDChannelModulator() = default;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  float g,
                  ModPacket &mpkt) override final;
protected:
    /** @brief Time domain resampler */
    Resampler resampler_;
};

#endif /* TDCHANNELMODULATOR_H_ */
