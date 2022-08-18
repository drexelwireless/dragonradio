// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef TDMA_H_
#define TDMA_H_

#include <vector>

#include "Radio.hh"
#include "mac/SlottedMAC.hh"
#include "phy/Channelizer.hh"
#include "phy/Synthesizer.hh"

/** @brief A TDMA MAC. */
class TDMA : public SlottedMAC
{
public:
    TDMA(std::shared_ptr<Radio> radio,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<Synthesizer> synthesizer,
         double rx_period);
    virtual ~TDMA();

    TDMA(const TDMA&) = delete;
    TDMA(TDMA&&) = delete;

    TDMA& operator=(const TDMA&) = delete;
    TDMA& operator=(TDMA&&) = delete;

private:
    void findNextSlot(WallClock::time_point t,
                      WallClock::time_point& t_next,
                      size_t& next_slotidx) override;

    void reconfigure(void) override;
};

#endif /* TDMA_H_ */
