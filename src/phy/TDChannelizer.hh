// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDCHANNELIZER_H_
#define TDCHANNELIZER_H_

#include <math.h>

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>

#include "SafeQueue.hh"
#include "dsp/Polyphase.hh"
#include "dsp/TableNCO.hh"
#include "phy/Channel.hh"
#include "phy/Channelizer.hh"

/** @brief A time-domain channelizer. */
class TDChannelizer : public Channelizer
{
public:
    TDChannelizer(const std::vector<PHYChannel> &channels,
                  double rx_rate,
                  unsigned int nthreads);
    virtual ~TDChannelizer();

    void push(const std::shared_ptr<IQBuf> &) override;

    /** @brief Stop demodulating. */
    void stop(void);

private:
    /** @brief Channel state for time-domain demodulation */
    class TDChannelDemodulator : public ChannelDemodulator {
    public:
        TDChannelDemodulator(unsigned chanidx,
                             const PHYChannel &channel,
                             double rx_rate)
          : ChannelDemodulator(chanidx, channel, rx_rate)
          , seq_(0)
          , delay_(round((channel.taps.size() - 1)/2.0))
          , resamp_buf_(0)
          , resamp_(rate_, 2*M_PI*channel.channel.fc/rx_rate, channel.taps)
        {
        }

        virtual ~TDChannelDemodulator() = default;

        /** @brief Update IQ buffer sequence number */
        void updateSeq(unsigned seq);

        void reset(void) override;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset) override;

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

    /** @brief Number of demodulation threads. */
    unsigned nthreads_;

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

    void reconfigure(void) override;

    void wake_dependents() override;
};

#endif /* TDCHANNELIZER_H_ */
