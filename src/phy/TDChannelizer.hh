// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDCHANNELIZER_H_
#define TDCHANNELIZER_H_

#include <math.h>

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "barrier.hh"
#include "SafeQueue.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A time-domain channelizer. */
class TDChannelizer : public Channelizer
{
public:
    TDChannelizer(std::shared_ptr<PHY> phy,
                  double rx_rate,
                  const Channels &channels,
                  unsigned int nthreads);
    virtual ~TDChannelizer();

    void push(const std::shared_ptr<IQBuf> &) override;

    void reconfigure(void) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain demodulation */
    class TDChannelDemodulator : public ChannelDemodulator {
    public:
        TDChannelDemodulator(PHY &phy,
                     const Channel &channel,
                     const std::vector<C> &taps,
                     double rx_rate)
          : ChannelDemodulator(phy, channel, taps, rx_rate)
          , seq_(0)
          , delay_(round((taps.size() - 1)/2.0))
          , resamp_buf_(0)
          , resamp_(rate_, 2*M_PI*channel.fc/rx_rate, taps)
        {
        }

        virtual ~TDChannelDemodulator() = default;

        /** @brief Update IQ buffer sequence number */
        void updateSeq(unsigned seq);

        void reset(void) override;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset,
                       float rx_rate) override;

        void demodulate(const std::complex<float>* data,
                        size_t count) override;

    protected:
        /** @brief Channel IQ buffer sequence number */
        unsigned seq_;

        /** @brief Filter delay */
        size_t delay_;

        /** @brief Resampling buffer */
        IQBuf resamp_buf_;

        /** @brief Resampler */
        dragonradio::signal::MixingRationalResampler<C,C> resamp_;
    };

    static const unsigned LOGN = 4;

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
    std::mutex demod_mutex_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<TDChannelDemodulator>> demods_;

    /** @brief Packets to demodulate */
    std::vector<std::unique_ptr<SafeQueue<std::shared_ptr<IQBuf>>>> iqbufs_;

    /** @brief Demodulation worker threads. */
    std::vector<std::thread> demod_threads_;

    /** @brief A reference to the global logger */
    std::shared_ptr<Logger> logger_;

    /** @brief A demodulation worker. */
    void demodWorker(unsigned tid);
};

#endif /* TDCHANNELIZER_H_ */
