#ifndef FDSYNTHESIZER_H_
#define FDSYNTHESIZER_H_

#include "dsp/FDResample.hh"
#include "phy/ChannelSynthesizer.hh"

/** @brief A frequency-domain synthesizer. */
class FDSynthesizer : public ChannelSynthesizer
{
public:
    /** @brief Filter length */
    /** We need two factors of 5 because we need to support 25MHz bandwidth.
     * The rest of the factors of 2 are for good measure.
     */
    static constexpr unsigned P = 25*64+1;

    /** @brief Overlap factor */
    static constexpr unsigned V = 8;

    using Upsampler = FDUpsampler<C,P,V>;

    FDSynthesizer(std::shared_ptr<PHY> phy,
                  double tx_rate,
                  const Channels &channels,
                  size_t nthreads)
      : ChannelSynthesizer(phy, tx_rate, channels, nthreads)
    {
    }

    virtual ~FDSynthesizer() = default;

private:
    /** @brief Channel state for frequency-domain modulation */
    class FDChannelState : public ChannelState, private Upsampler {
    public:
        FDChannelState(PHY &phy,
                       unsigned chanidx,
                       const Channel &channel,
                       const std::vector<C> &taps,
                       double tx_rate)
          : ChannelState(phy, chanidx, channel, taps, tx_rate)
          , Upsampler(phy.getMinTXRateOversample(), tx_rate/channel.bw, N*(channel.fc/tx_rate))
        {
        }

        FDChannelState() = delete;

        virtual ~FDChannelState() = default;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      float g,
                      ModPacket &mpkt) override final;

    protected:
        /** @brief OLS time domain converter */
        Upsampler::ToTimeDomain timedomain_;
    };

    std::unique_ptr<ChannelState>
    mkChannelState(unsigned chanidx,
                   const Channel &channel,
                   const std::vector<C> &taps,
                   double tx_rate) override final
    {
        return std::unique_ptr<ChannelState>(new FDChannelState(*phy_,
                                                                chanidx,
                                                                channel,
                                                                taps,
                                                                tx_rate));
    }
};

#endif /* FDSYNTHESIZER_H_ */
