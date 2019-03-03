#ifndef CHANNELMODULATOR_H_
#define CHANNELMODULATOR_H_

#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/PHY.hh"

/** @brief A time-domain channel modulator that performs mixing, filtering, and
 * upsampling.
 */
class ChannelModulator {
public:
    using C = std::complex<float>;

    ChannelModulator(PHY &phy,
                     const std::vector<C> &taps,
                     double rate,
                     double rad)
      : rate_(rate)
      , rad_(rad)
      , resamp_(rate, taps)
      , mod_(phy.mkModulator())
    {
        resamp_.setFreqShift(rad);
    }

    ChannelModulator() = delete;
    ChannelModulator(const ChannelModulator&) = delete;
    ChannelModulator(ChannelModulator&&) = delete;

    ~ChannelModulator() = default;

    ChannelModulator &operator =(const ChannelModulator&) = delete;
    ChannelModulator &operator =(ChannelModulator &&) = delete;

    /** @brief Set prototype filter. Should have unity gain. */
    const std::vector<C> &getTaps(void) const
    {
        return resamp_.getTaps();
    }

    /** @brief Set prototype filter. Should have unity gain. */
    void setTaps(const std::vector<C> &taps)
    {
        resamp_.setTaps(taps);
    }

    /** @brief Set resampling rate */
    void setRate(double rate)
    {
        if (rate_ != rate) {
            rate_ = rate;
            resamp_.setRate(rate_);
        }
    }

    /** @brief Set frequency shift */
    void setFreqShift(double rad)
    {
        if (rad != rad_) {
            rad_ = rad;
            resamp_.setFreqShift(rad_);
        }
    }

    /** @brief Reset internal state */
    void reset(const Channel &channel)
    {
        resamp_.reset();
    }

    /** @brief Modulate a packet to produce IQ samples.
     * @param channel The channel being modulated.
     * @param pkt The NetPacket to modulate.
     * @param mpkt The ModPacket in which to place modulated samples.
     */
    void modulate(const Channel &channel,
                  std::shared_ptr<NetPacket> pkt,
                  ModPacket &mpkt);

protected:
    /** @brief Resampling rate */
    double rate_;

    /** @brief Frequency shift in radians, i.e., 2*M_PI*shift/Fs */
    double rad_;

    /** @brief Resampler */
    Dragon::MixingRationalResampler<C,C> resamp_;

    /** @brief Our demodulator */
    std::shared_ptr<PHY::Modulator> mod_;
};

#endif /* CHANNELMODULATOR_H_ */
