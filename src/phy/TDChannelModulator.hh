#ifndef TDCHANNELMODULATOR_H_
#define TDCHANNELMODULATOR_H_

#include "dsp/Polyphase.hh"
#include "phy/Synthesizer.hh"

/** @brief Channel state for time-domain modulation */
class TDChannelModulator : public ChannelModulator {
public:
    using C = std::complex<float>;

    using Upsampler = Dragon::MixingRationalResampler<C,C>;

    TDChannelModulator(PHY &phy,
                       unsigned chanidx,
                       const Channel &channel,
                       const std::vector<C> &taps,
                       double tx_rate)
      : ChannelModulator(phy, chanidx, channel, taps, tx_rate)
      , upsampler_(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw),
                   taps)
    {
        upsampler_.setFreqShift(2*M_PI*channel.fc/tx_rate);
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
