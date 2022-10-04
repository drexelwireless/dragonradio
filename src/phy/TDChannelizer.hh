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
    class TDChannelDemodulator;

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
