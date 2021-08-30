// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef OVERLAPTDCHANNELIZER_H_
#define OVERLAPTDCHANNELIZER_H_

#include <math.h>

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "Logger.hh"
#include "RadioPacketQueue.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A time-domain channelizer that demodulates overlapping pairs of
 * slots. This duplicates work (and leads to duplicate packets), but it allows
 * us to parallelize demodulation of *a single channel*. We have to do this when
 * demodulation is slow, such as when we use liquid's resamplers.
 */
class OverlapTDChannelizer : public Channelizer
{
public:
    using C = std::complex<float>;

    OverlapTDChannelizer(const std::vector<PHYChannel> &channels,
                         double rx_rate,
                         unsigned int nthreads);
    virtual ~OverlapTDChannelizer();

    void setChannels(const std::vector<PHYChannel> &channels) override;

    void push(const std::shared_ptr<IQBuf> &) override;

    void reconfigure(void) override;

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

private:
    /** @brief Channel state for time-domain demodulation */
    class OverlapTDChannelDemodulator : public ChannelDemodulator {
    public:
        OverlapTDChannelDemodulator(const PHYChannel &channel,
                                    double rx_rate)
          : ChannelDemodulator(channel, rx_rate)
          , delay_(round((channel.taps.size() - 1)/2.0))
          , rx_rate_(rx_rate)
          , rx_oversample_(channel.phy->getMinRXRateOversample())
          , resamp_buf_(0)
          , resamp_(rate_, 2*M_PI*channel.channel.fc/rx_rate, channel.taps)
        {
        }

        virtual ~OverlapTDChannelDemodulator() = default;

        /** @brief Set channel */
        void setChannel(const PHYChannel &channel);

        void reset(void) override;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset,
                       float rx_rate) override;

        void demodulate(const std::complex<float>* data,
                        size_t count) override;

    protected:
        /** @brief Filter delay */
        size_t delay_;

        /** @brief RX rate */
        const double rx_rate_;

        /** @brief RX oversample factor */
        const unsigned rx_oversample_;

        /** @brief Resampling buffer */
        IQBuf resamp_buf_;

        /** @brief Resampler */
        dragonradio::signal::MixingRationalResampler<C,C> resamp_;
    };

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
    std::vector<PHYChannel>::size_type iq_next_channel_;

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
