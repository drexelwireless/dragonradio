#ifndef UNICHANNELSYNTHESIZER_HH_
#define UNICHANNELSYNTHESIZER_HH_

#include <atomic>

#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/SlotSynthesizer.hh"

/** @brief A single-channel synthesizer. */
template <class ChannelModulator>
class UnichannelSynthesizer : public SlotSynthesizer
{
public:
    UnichannelSynthesizer(std::shared_ptr<PHY> phy,
                          double tx_rate,
                          const Channels &channels,
                          size_t nthreads);

    virtual ~UnichannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void reconfigure(void) override;

    void stop(void) override;

protected:
    /** @brief Flag indicating if we should stop processing packets */
    std::atomic<bool> done_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig, unsigned tid);
};

#endif /* UNICHANNELSYNTHESIZER_HH_ */
