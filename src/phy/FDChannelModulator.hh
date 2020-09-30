#ifndef FDCHANNELMODULATOR_H_
#define FDCHANNELMODULATOR_H_

#include "dsp/FDResample.hh"
#include "phy/Synthesizer.hh"

#if defined(DOXYGEN)
#define final
#endif /* defined(DOXYGEN) */

/** @brief Channel state for frequency-domain modulation */
class FDChannelModulator : public ChannelModulator {
public:
    using C = std::complex<float>;

    /** @brief Filter length */
    /** We need two factors of 5 because we need to support 25MHz bandwidth.
     * The rest of the factors of 2 are for good measure.
     */
    static constexpr unsigned P = 25*64+1;

    /** @brief Overlap factor */
    static constexpr unsigned V = 8;

    using Upsampler = FDUpsampler<C,P,V>;

    static constexpr auto N = Upsampler::N;

    FDChannelModulator(PHY &phy,
                       unsigned chanidx,
                       const Channel &channel,
                       const std::vector<C> &taps,
                       double tx_rate)
      : ChannelModulator(phy, chanidx, channel, taps, tx_rate)
      , upsampler_(phy.getMinTXRateOversample(), tx_rate/channel.bw, N*(channel.fc/tx_rate))
    {
    }

    FDChannelModulator() = delete;

    virtual ~FDChannelModulator() = default;

    void modulate(std::shared_ptr<NetPacket> pkt,
                  float g,
                  ModPacket &mpkt) override final;

protected:
    /** @brief Frequency domain upsampler */
    Upsampler upsampler_;

    /** @brief OLS time domain converter */
    Upsampler::ToTimeDomain timedomain_;
};

#endif /* FDCHANNELMODULATOR_H_ */
