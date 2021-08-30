// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef PARALLELCHANNELSYNTHESIZER_HH_
#define PARALLELCHANNELSYNTHESIZER_HH_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include "barrier.hh"
#include "phy/Channel.hh"
#include "phy/ChannelSynthesizer.hh"
#include "phy/PHY.hh"

/** @brief Use one or more workers to synthesize packets for a single channel. */
template <class ChannelModulator>
class ParallelChannelSynthesizer : public ChannelSynthesizer
{
public:
    ParallelChannelSynthesizer(double tx_rate,
                               const std::vector<PHYChannel> &channels,
                               size_t nthreads);

    virtual ~ParallelChannelSynthesizer();

    void stop(void) override;

    void reconfigure(void) override;

protected:
    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Flag indicating if we should stop processing packets */
    std::atomic<bool> done_;

    /** @brief Flag that is true when we are reconfiguring. */
    std::atomic<bool> reconfigure_;

    /** @brief Reconfiguration barrier */
    barrier reconfigure_sync_;

    /** @brief Mutex for waking demodulators. */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking demodulators. */
    std::condition_variable wake_cond_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);
};

#endif /* PARALLELCHANNELSYNTHESIZER_HH_ */
