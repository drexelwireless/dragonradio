#ifndef TDCHANNELIZER_H_
#define TDCHANNELIZER_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "barrier.hh"
#include "ringbuffer.hh"
#include "spinlock_mutex.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A time-domain channelizer. */
class TDChannelizer : public Channelizer, public Element
{
public:
    TDChannelizer(std::shared_ptr<Net> net,
                  std::shared_ptr<PHY> phy,
                  const Channels &channels,
                  unsigned int nthreads);
    virtual ~TDChannelizer();

    void setChannels(const Channels &channels) override;

    void push(const std::shared_ptr<IQBuf> &) override;

    void reconfigure(void) override;

    /** @brief Get prototype filter for channelization. */
    const std::vector<C> &getTaps(void) const
    {
        return taps_;
    }

    /** @brief Set prototype filter for channelization. */
    /** The prototype filter should have unity gain. */
    void setTaps(const std::vector<C> &taps)
    {
        taps_ = taps;
        reconfigure();
    }

    /** @brief Stop demodulating. */
    void stop(void);

    /** @brief Demodulated packets */
    RadioOut<Push> source;

private:
    /** @brief Channel state for time-domain demodulation */
    class ChannelState {
    public:
        ChannelState(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double rate,
                     double rad)
          : channel_(channel)
          , rate_(rate)
          , rad_(rad)
          , resamp_(rate, taps)
          , demod_(phy.mkDemodulator())
          , seq_(0)
        {
            resamp_.setFreqShift(rad);
        }

        ~ChannelState() = default;

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

        /** @brief Update IQ buffer equence number */
        void updateSeq(unsigned seq)
        {
            // Reset state if we have a discontinuity or if we're not currently
            // receiving a frame
            if (seq != seq_ + 1 || !isFrameOpen())
                reset();

            // Record buffer sequence number
            seq_ = seq;
        }

        /** @brief Is a frame currently being demodulated?
         * @return true if a frame is currently being demodulated, false
         * otherwise.
         */
        bool isFrameOpen(void)
        {
            return demod_->isFrameOpen();
        }

        /** @brief Reset internal state */
        void reset(void)
        {
            resamp_.reset();
            demod_->reset(channel_);
            seq_ = 0;
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
        /** @brief Channel we are demodulating */
        Channel channel_;

        /** @brief Resampling rate */
        double rate_;

        /** @brief Frequency shift in radians, i.e., 2*M_PI*shift/Fs */
        double rad_;

        /** @brief Resampler */
        Dragon::MixingRationalResampler<C,C> resamp_;

        /** @brief Our demodulator */
        std::shared_ptr<PHY::Demodulator> demod_;

        /** @brief Channel IQ buffer sequence number */
        unsigned seq_;
    };

    static const unsigned LOGN = 4;

    /** @brief Destination for packets. */
    std::shared_ptr<Net> net_;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy_;

    /** @brief Prototype filter */
    std::vector<C> taps_;

    /** @brief Number of demodulation threads. */
    unsigned nthreads_;

    /** @brief Flag that is true when we should finish processing. */
    bool done_;

    /** @brief Flag that is true when we are reconfiguring. */
    std::atomic<bool> reconfigure_;

    /** @brief Reconfiguration barrier */
    barrier reconfigure_sync_;

    /** @brief Mutex for waking demodulators. */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking demodulators. */
    std::condition_variable wake_cond_;

    /** @brief Mutex for demodulation state. */
    spinlock_mutex demod_mutex_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<ChannelState>> demods_;

    /** @brief Packets to demodulate */
    std::unique_ptr<ringbuffer<std::shared_ptr<IQBuf>, LOGN> []> iqbufs_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief Get RX downsample rate for given channel. */
    double getRXDownsampleRate(const Channel &channel)
    {
        if (channel.bw == 0.0)
            return 1.0;
        else
            return (phy_->getMinTXRateOversample()*channel.bw)/rx_rate_;
    }

    /** @brief A demodulation worker. */
    void demodWorker(unsigned tid);
};

#endif /* TDCHANNELIZER_H_ */
