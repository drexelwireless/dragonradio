#ifndef OVERLAPTDCHANNELIZER_H_
#define OVERLAPTDCHANNELIZER_H_

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "spinlock_mutex.hh"
#include "Logger.hh"
#include "RadioPacketQueue.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "net/Net.hh"

/** @brief A time-domain channelizer that demodulates overlapping pairs of
 * slots. This duplicates work (and leads to duplicate packets), but it allows
 * us to parallelize demodulation of *a single channel*. We have to do this when
 * demodulation is slow, such as when we use liquid's resamplers.
 */
class OverlapTDChannelizer : public Channelizer, public Element
{
public:
    using C = std::complex<float>;

    OverlapTDChannelizer(std::shared_ptr<Net> net,
                         std::shared_ptr<PHY> phy,
                         double rx_rate,
                         const Channels &channels,
                         unsigned int nthreads);
    virtual ~OverlapTDChannelizer();

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

    /** @brief Return the portion of the end of the previous slot that we
     * demodulate.
     */
    double getPrevDemod(void)
    {
        return prev_demod_;
    }

    /** @brief Set the portion of the end of the previous slot that we
     * demodulate.
     */
    void setPrevDemod(double sec)
    {
        prev_demod_ = sec;
        reconfigure();
    }

    /** @brief Return the portion of the current slot that we demodulate. */
    double getCurDemod(void)
    {
        return cur_demod_;
    }

    /** @brief Set the portion of the current slot that we demodulate. */
    void setCurDemod(double sec)
    {
        cur_demod_ = sec;
        reconfigure();
    }

    /** @brief Return flag indicating whether or not demodulation queue enforces
     * packet order.
     */
    bool getEnforceOrdering(void)
    {
        return enforce_ordering_;
    }

    /** @brief Set whether or not demodulation queue enforces packet order. */
    void setEnforceOrdering(bool enforce)
    {
        enforce_ordering_ = enforce;
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

        ~ChannelState() = default;

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

    /** @brief Destination for packets. */
    std::shared_ptr<Net> net_;

    /** @brief PHY we use for demodulation. */
    std::shared_ptr<PHY> phy_;

    /** @brief Prototype filter */
    std::vector<C> taps_;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double slot_size_;

    /** @brief What portion of the end of the previous slot should we
     * demodulate (sec)?
     */
    double prev_demod_;

    /** @brief How many samples from the end of the previous slot should we
     * demodulate?
     */
    size_t prev_demod_samps_;

    /** @brief What portion of the current slot should we demodulate (sec)? */
    double cur_demod_;

    /** @brief How many samples from the current slot should we demodulate? */
    size_t cur_demod_samps_;

    /** @brief Should packets be output in the order they were actually
     * received? Setting this to true increases latency!
     */
    bool enforce_ordering_;

    /** @brief Flag that is true when we should finish processing. */
    bool done_;

    /** @brief Queue of radio packets. */
    RadioPacketQueue radio_q_;

    /** @brief Mutex protecting the queue of IQ buffers. */
    std::mutex iq_mutex_;

    /** @brief Condition variable protecting the queue of IQ buffers. */
    std::condition_variable iq_cond_;

    /** @brief The number of items in the queue of IQ buffers. */
    size_t iq_size_;

    /** @brief The next channel to demodulate. */
    Channels::size_type iq_next_channel_;

    /** @brief The queue of IQ buffers. */
    std::list<std::shared_ptr<IQBuf>> iq_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> demod_reconfigure_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief Network send thread. */
    std::thread net_thread_;

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
    void demodWorker(std::atomic<bool> &reconfig);

    /** @brief The network send worker. */
    void netWorker(void);

    /** @brief Get two slot's worth of IQ data.
     * @param b The barrier before which network packets should be inserted.
     * @param channel The channel to demodulate.
     * @param buf1 The buffer holding the previous slot's IQ data.
     * @param buf2 The buffer holding the current slot's IQ data.
     * @return Return true if pop was successful, false otherwise.
     */
    /** Return two slot's worth of IQ data---the previous slot, and the current
     * slot. The previous slot is removed from the queue, whereas the current
     * slot is kept in the queue because it becomes the new "previous" slot.
     */
    bool pop(RadioPacketQueue::barrier& b,
             unsigned &channel,
             std::shared_ptr<IQBuf>& buf1,
             std::shared_ptr<IQBuf>& buf2);

     /** @brief Move to the next demodulation window. */
     void nextWindow(void);
};

#endif /* OVERLAPTDCHANNELIZER_H_ */
