// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "logging.hh"
#include "Clock.hh"
#include "Radio.hh"
#include "mac/SlottedALOHA.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

SlottedALOHA::SlottedALOHA(std::shared_ptr<Radio> radio,
                           std::shared_ptr<Controller> controller,
                           std::shared_ptr<SnapshotCollector> collector,
                           std::shared_ptr<Channelizer> channelizer,
                           std::shared_ptr<Synthesizer> synthesizer,
                           double rx_period,
                           double p)
  : SlottedMAC(radio,
               controller,
               collector,
               channelizer,
               synthesizer,
               rx_period,
               5)
  , slotidx_(0)
  , p_(p)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
{
    rx_thread_ = std::thread(&SlottedALOHA::rxWorker, this);
    tx_thread_ = std::thread(&SlottedALOHA::txWorker, this);
    tx_slot_thread_ = std::thread(&SlottedALOHA::txSlotWorker, this);
    tx_notifier_thread_ = std::thread(&SlottedALOHA::txNotifier, this);

    modify([&]() { reconfigure(); });
}

SlottedALOHA::~SlottedALOHA()
{
    stop();
}

void SlottedALOHA::findNextSlot(WallClock::time_point t,
                                WallClock::time_point& t_next,
                                size_t& next_slotidx)
{
    WallClock::duration t_slot_pos; // Offset into the current slot (sec)
    const auto          slot_size = schedule_.getSlotSize();

    t_slot_pos = schedule_.slotOffsetAt(t);

    t_next = t + (slot_size - t_slot_pos);
    next_slotidx = getSlotIndex();
}

bool SlottedALOHA::transmitInSlot(WallClock::time_point t,
                                  size_t slotidx)
{
    return dist_(gen_) < p_;
}

void SlottedALOHA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    // We can always transmit
    can_transmit_ = true;
}
