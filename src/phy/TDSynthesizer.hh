#ifndef TDSYNTHESIZER_H_
#define TDSYNTHESIZER_H_

#include "dsp/Polyphase.hh"
#include "phy/ChannelSynthesizer.hh"

/** @brief A time-domain synthesizer. */
class TDSynthesizer : public ChannelSynthesizer
{
public:
    using Upsampler = Dragon::MixingRationalResampler<C,C>;

    TDSynthesizer(std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channels &channels,
                  size_t nthreads)
      : ChannelSynthesizer(phy, tx_rate, channels, nthreads)
    {
    }

    virtual ~TDSynthesizer() = default;

private:
    /** @brief Channel state for time-domain modulation */
    class TDChannelState : public ChannelState, private Upsampler {
    public:
        TDChannelState(PHY &phy,
                       unsigned chanidx,
                       const Channel &channel,
                       const std::vector<C> &taps,
                       double tx_rate)
          : ChannelState(phy, chanidx, channel, taps, tx_rate)
          , Upsampler(channel.bw == 0.0 ? 1.0 : tx_rate/(phy.getMinTXRateOversample()*channel.bw),
                      taps)
        {
            setFreqShift(2*M_PI*channel.fc/tx_rate);
        }

        void reset(void) override
        {
            Upsampler::reset();
        }

        TDChannelState() = delete;

        virtual ~TDChannelState() = default;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      float g,
                      ModPacket &mpkt) override final;
    };

    std::unique_ptr<ChannelState>
    mkChannelState(unsigned chanidx,
                   const Channel &channel,
                   const std::vector<C> &taps,
                   double tx_rate) override final
    {
        return std::unique_ptr<ChannelState>(new TDChannelState(*phy_,
                                                                chanidx,
                                                                channel,
                                                                taps,
                                                                tx_rate));
    }
};

#endif /* TDSYNTHESIZER_H_ */
