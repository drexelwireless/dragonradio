// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "Radio.hh"
#include "mac/SlottedALOHA.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

SlottedALOHA::SlottedALOHA(std::shared_ptr<Radio> radio,
                           std::shared_ptr<Controller> controller,
                           std::shared_ptr<SnapshotCollector> collector,
                           std::shared_ptr<Channelizer> channelizer,
                           std::shared_ptr<SlotSynthesizer> synthesizer,
                           double slot_size,
                           double guard_size,
                           double slot_send_lead_time,
                           double p)
  : SlottedMAC(radio,
               controller,
               collector,
               channelizer,
               synthesizer,
               slot_size,
               guard_size,
               slot_send_lead_time)
  , slotidx_(0)
  , p_(p)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
  , arrival_dist_(p)
{
    reconfigure();

    rx_thread_ = std::thread(&SlottedALOHA::rxWorker, this);
    tx_thread_ = std::thread(&SlottedALOHA::txWorker, this);
    tx_slot_thread_ = std::thread(&SlottedALOHA::txSlotWorker, this);
    tx_notifier_thread_ = std::thread(&SlottedALOHA::txNotifier, this);
}

SlottedALOHA::~SlottedALOHA()
{
    stop();
}

void SlottedALOHA::stop(void)
{
    SlottedMAC::stop();

    tx_records_cond_.notify_all();

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();

    if (tx_slot_thread_.joinable())
        tx_slot_thread_.join();

    if (tx_notifier_thread_.joinable())
        tx_notifier_thread_.join();
}

void SlottedALOHA::txSlotWorker(void)
{
    WallClock::time_point t_now;            // Current time
    WallClock::time_point t_next_slot;      // Time at which our next slot starts
    WallClock::time_point t_following_slot; // Time at which the following slot starts
    WallClock::duration   t_slot_pos;       // Offset into the current slot (sec)

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = WallClock::now();
        t_slot_pos = t_now.time_since_epoch() % slot_size_;
        t_next_slot = t_now + (slot_size_ - t_slot_pos);
        t_following_slot = t_next_slot + slot_size_;

        // Finalize next slot
        auto slot = finalizeSlot(t_next_slot);

        // Modulate following slot with probability p_
        if (dist_(gen_) < p_)
            modulateSlot(t_following_slot, 0, slotidx_);

        // Transmit next slot
        if (slot)
            txSlot(std::move(slot));

        // Sleep until TX time for following slot
        sleep_for((t_following_slot - WallClock::now()) - slot_send_lead_time_);
    }

    if (next_slot_)
        missedSlot(*next_slot_);
}

void SlottedALOHA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    if (schedule_.size() == 0 || slotidx_ >= schedule_[0].size())
        slotidx_ = 0;
}
