#ifndef CHANNELDEMODULATOR_H_
#define CHANNELDEMODULATOR_H_

#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/PHY.hh"

/** @brief A time-domain channel demodulator that performs mixing, filtering,
 * and downsampling.
 */
class ChannelDemodulator {
public:
    using C = std::complex<float>;

    ChannelDemodulator(PHY &phy,
                       const std::vector<C> &taps,
                       double rate,
                       double rad)
      : rate_(rate)
      , rad_(rad)
      , resamp_(rate, taps)
      , demod_(phy.mkDemodulator())
    {
        resamp_.setFreqShift(rad);
    }

    ChannelDemodulator() = delete;
    ChannelDemodulator(const ChannelDemodulator&) = delete;
    ChannelDemodulator(ChannelDemodulator&&) = delete;

    ~ChannelDemodulator() = default;

    ChannelDemodulator &operator =(const ChannelDemodulator&) = delete;
    ChannelDemodulator &operator =(ChannelDemodulator &&) = delete;

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
        demod_->reset(channel);
    }

    /** @brief Set timestamp for demodulation
     * @param timestamp The timestamp for future samples.
     * @param snapshot_off The snapshot offset associated with the given
     * timestamp.
     * @param offset The offset of the first sample that will be demodulated.
     */
     virtual void timestamp(const MonoClock::time_point &timestamp,
                            std::optional<size_t> snapshot_off,
                            size_t offset)
     {
         demod_->timestamp(timestamp,
                           snapshot_off,
                           offset,
                           rate_);
     }

    /** @brief Demodulate data with given parameters */
    void demodulate(IQBuf &resamp_buf,
                    const std::complex<float>* data,
                    size_t count,
                    std::function<void(std::unique_ptr<RadioPacket>)> callback);

protected:
    /** @brief Resampling rate */
    double rate_;

    /** @brief Frequency shift in radians, i.e., 2*M_PI*shift/Fs */
    double rad_;

    /** @brief Resampler */
    Dragon::MixingRationalResampler<C,C> resamp_;

    /** @brief Our demodulator */
    std::shared_ptr<PHY::Demodulator> demod_;
};

#endif /* CHANNELDEMODULATOR_H_ */
