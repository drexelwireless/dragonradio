#ifndef FDMA_H_
#define FDMA_H_

#include <vector>

#include "USRP.hh"
#include "phy/Channelizer.hh"
#include "phy/PHY.hh"
#include "phy/ChannelSynthesizer.hh"
#include "mac/MAC.hh"

/** @brief A FDMA MAC. */
class FDMA : public MAC
{
public:
    FDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<ChannelSynthesizer> synthesizer,
         double period);
    virtual ~FDMA();

    FDMA(const FDMA&) = delete;
    FDMA(FDMA&&) = delete;

    FDMA& operator=(const FDMA&) = delete;
    FDMA& operator=(FDMA&&) = delete;

    /** @brief Stop processing packets */
    void stop(void) override;

    void reconfigure(void) override;

private:
    /** @brief Amount of data to pre-modulate (sec) */
    double premod_;

    /** @brief Out channel synthesizer */
    std::shared_ptr<ChannelSynthesizer> channel_synthesizer_;

    /** @brief Thread running rxWorker */
    std::thread rx_thread_;

    /** @brief Thread running txWorker */
    std::thread tx_thread_;

    /** @brief Thread running txNotifier */
    std::thread tx_notifier_thread_;

    /** @brief Worker preparing slots for transmission */
    void txWorker(void);
};

#endif /* FDMA_H_ */
