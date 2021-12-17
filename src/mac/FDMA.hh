// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FDMA_H_
#define FDMA_H_

#include <chrono>
#include <vector>

#include "Radio.hh"
#include "phy/Channelizer.hh"
#include "phy/ChannelSynthesizer.hh"
#include "mac/MAC.hh"

/** @brief A FDMA MAC. */
class FDMA : public MAC
{
public:
    FDMA(std::shared_ptr<Radio> radio,
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

    bool getAccurateTXTimestamps(void) const
    {
        return accurate_tx_timestamps_;
    }

    void setAccurateTXTimestamps(bool accurate)
    {
        accurate_tx_timestamps_ = accurate;
    }

    std::chrono::duration<double> getTimedTXDelay(void) const
    {
        return timed_tx_delay_;
    }

    void setTimedTXDelay(std::chrono::duration<double> t)
    {
        timed_tx_delay_ = t;
    }

    /** @brief Stop processing packets */
    void stop(void) override;

    void reconfigure(void) override;

private:
    /** @brief Amount of data to pre-modulate (sec) */
    double premod_;

    /** @brief Provide more accurate TX timestamps */
    /** Providing more accurate TX timestamps may increase latency. */
    bool accurate_tx_timestamps_;

    /** @brief Delay for timed TX (sec) */
    std::chrono::duration<double> timed_tx_delay_;

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
