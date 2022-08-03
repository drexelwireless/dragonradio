// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <chrono>

#include "logging.hh"
#include "Clock.hh"
#include "Radio.hh"
#include "mac/TDMA.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

TDMA::TDMA(std::shared_ptr<Radio> radio,
           std::shared_ptr<Controller> controller,
           std::shared_ptr<SnapshotCollector> collector,
           std::shared_ptr<Channelizer> channelizer,
           std::shared_ptr<Synthesizer> synthesizer,
           double rx_period)
  : SlottedMAC(radio,
               controller,
               collector,
               channelizer,
               synthesizer,
               rx_period,
               5)
{
    rx_thread_ = std::thread(&TDMA::rxWorker, this);
    tx_thread_ = std::thread(&TDMA::txWorker, this);
    tx_slot_thread_ = std::thread(&TDMA::txSlotWorker, this);
    tx_notifier_thread_ = std::thread(&TDMA::txNotifier, this);

    modify([&]() { reconfigure(); });
}

TDMA::~TDMA()
{
    stop();
}

void TDMA::findNextSlot(WallClock::time_point t,
                        WallClock::time_point& t_next,
                        size_t &next_slotidx)
{
    size_t              cur_slot;   // Current slot index
    WallClock::duration t_slot_pos; // Offset into the current slot (sec)
    const auto          slot_size = schedule_.getSlotSize();
    const auto          nslots = schedule_.nslots();
    size_t              slotidx;

    cur_slot = schedule_.slotAt(t);
    t_slot_pos = schedule_.slotOffsetAt(t);

    for (size_t tx_slot = 1; tx_slot <= nslots; ++tx_slot) {
        slotidx = (cur_slot + tx_slot) % nslots;

        if (schedule_.canTransmitInSlot(slotidx)) {
            t_next = t + (tx_slot*slot_size - t_slot_pos);
            next_slotidx = slotidx;
            return;
        }
    }

    assert(false);
}

void TDMA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    // Determine whether or not we can transmit
    can_transmit_ = schedule_.canTransmit();
}
