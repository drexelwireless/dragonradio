// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UNICHANNELSYNTHESIZER_HH_
#define UNICHANNELSYNTHESIZER_HH_

#include <atomic>
#include <mutex>

#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/SlotSynthesizer.hh"

/** @brief A single-channel synthesizer. */
template <class ChannelModulator>
class UnichannelSynthesizer : public SlotSynthesizer
{
public:
    UnichannelSynthesizer(const std::vector<PHYChannel> &channels,
                          double tx_rate,
                          size_t nthreads);

    virtual ~UnichannelSynthesizer();

    void stop(void) override;

    void modulate(const std::shared_ptr<Slot> &slot) override;

protected:
    /** @brief Lock for current slot */
    std::mutex curslot_mutex_;

    /** @brief Condition variable for current slot */
    std::condition_variable curslot_cond_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);

    void wake_dependents() override;
};

#endif /* UNICHANNELSYNTHESIZER_HH_ */
