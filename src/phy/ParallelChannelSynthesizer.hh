// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef PARALLELCHANNELSYNTHESIZER_HH_
#define PARALLELCHANNELSYNTHESIZER_HH_

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

#include "phy/Channel.hh"
#include "phy/ChannelSynthesizer.hh"
#include "phy/PHY.hh"

/** @brief Use one or more workers to synthesize packets for a single channel. */
template <class ChannelModulator>
class ParallelChannelSynthesizer : public ChannelSynthesizer
{
public:
    ParallelChannelSynthesizer(const std::vector<PHYChannel> &channels,
                               double tx_rate,
                               size_t nthreads);

    virtual ~ParallelChannelSynthesizer();

    void stop(void) override;

protected:
    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Mutex for waking demodulators. */
    std::mutex wake_mutex_;

    /** @brief Condition variable for waking demodulators. */
    std::condition_variable wake_cond_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);

    void wake_dependents() override;
};

#endif /* PARALLELCHANNELSYNTHESIZER_HH_ */
