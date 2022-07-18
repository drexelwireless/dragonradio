// Copyright 2018-2022 Drexel University
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

protected:
    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);
};

#endif /* PARALLELCHANNELSYNTHESIZER_HH_ */
