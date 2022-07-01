// Copyright 2018-2021 Drexel University
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
           std::shared_ptr<SlotSynthesizer> synthesizer,
           double rx_period,
           double slot_send_lead_time)
  : SlottedMAC(radio,
               controller,
               collector,
               channelizer,
               synthesizer,
               rx_period,
               slot_send_lead_time,
               5)
{
    reconfigure();

    rx_thread_ = std::thread(&TDMA::rxWorker, this);
    tx_thread_ = std::thread(&TDMA::txWorker, this);
    tx_slot_thread_ = std::thread(&TDMA::txSlotWorker, this);
    tx_notifier_thread_ = std::thread(&TDMA::txNotifier, this);
}

TDMA::~TDMA()
{
    stop();
}

void TDMA::stop(void)
{
    SlottedMAC::stop();

    if (modify([&](){ done_ = true; })) {
        // Join on all threads
        if (rx_thread_.joinable())
            rx_thread_.join();

        if (tx_thread_.joinable())
            tx_thread_.join();

        if (tx_slot_thread_.joinable())
            tx_slot_thread_.join();

        if (tx_notifier_thread_.joinable())
            tx_notifier_thread_.join();
    }
}

void TDMA::txSlotWorker(void)
{
    WallClock::time_point t_now;              // Current time
    WallClock::time_point t_next_slot;        // Time at which our next slot starts
    WallClock::time_point t_following_slot;   // Time at which our following slot starts
    size_t                next_slotidx;       // Slot index of next slot
    size_t                following_slotidx;  // Slot index of following slot
    size_t                noverfill = 0;      // Number of overfilled samples
    size_t                noverfillslots = 0; // Number of overfilled slots

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                break;
        }

        t_now = WallClock::now();

        // If we missed a slot, find the next slot
        if (t_now > t_next_slot) {
            if (!findNextSlot(t_now, t_next_slot, next_slotidx)) {
                logMAC(LOGDEBUG, "NO SLOT");
                // Sleep for 100ms if we don't yet have a slot
                sleep_for(100ms);
                continue;
            }
        }

        // If we're less that one slot away from our next slot, finalize it
        // and transmit it
        auto slot_size = schedule_.getSlotSize();

        if (t_next_slot - t_now < slot_size) {
            // Finalize next slot. After this returns, we have EXCLUSIVE access
            // to the slot.
            auto slot = finalizeSlot(t_next_slot);

            // Determine how many samples were sent beyond the end of the slot,
            // i.e., the number of overfilled samples.
            if (slot) {
                noverfill = slot->length() < slot->max_samples ? 0 : slot->length() - slot->max_samples;

                noverfill %= tx_full_slot_samps_;
                noverfillslots = noverfill / tx_full_slot_samps_;
            } else {
                noverfill = 0;
                noverfillslots = 0;
            }

            // Find following slot. We divide slot_size by two to avoid possible
            // rounding issues where we mights end up skipping a slot. If we
            // reach this line of code, then findNextSlot must have returned
            // true above, in which case we know it will return true here, so we
            // do not need to check the result.
            (void) findNextSlot(t_next_slot + noverfillslots*slot_size + slot_size/2.0,
                                t_following_slot,
                                following_slotidx);

            // Schedule modulation of following slot
            modulateSlot(t_following_slot,
                         noverfill,
                         following_slotidx);

            // Transmit next slot
            if (slot)
                txSlot(std::move(slot));

            // The following slot is now the next slot
            t_next_slot = t_following_slot;
            next_slotidx = following_slotidx;
        }

        // Sleep until it's time to send the next slot
        sleep_for((t_next_slot - WallClock::now()) - slot_send_lead_time_);
    }

    if (next_slot_)
        missedSlot(*next_slot_);
}

bool TDMA::findNextSlot(WallClock::time_point t,
                        WallClock::time_point &t_next,
                        size_t &next_slotidx)
{
    size_t              cur_slot;   // Current slot index
    WallClock::duration t_slot_pos; // Offset into the current slot (sec)
    const auto          slot_size = schedule_.getSlotSize();
    const auto          nslots = schedule_.nslots();

    cur_slot = schedule_.slotAt(t);
    t_slot_pos = schedule_.slotOffsetAt(t);

    for (size_t tx_slot = 1; tx_slot <= nslots; ++tx_slot) {
        if (tdma_schedule_[(cur_slot + tx_slot) % nslots]) {
            t_next = t + (tx_slot*slot_size - t_slot_pos);
            next_slotidx = (cur_slot + tx_slot) % nslots;
            return true;
        }
    }

    return false;
}

void TDMA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    size_t nslots = schedule_.nslots();

    tdma_schedule_.resize(nslots);
    for (size_t i = 0; i < nslots; ++i)
        tdma_schedule_[i] = schedule_.canTransmitInSlot(i);

    // Determine whether or not we have a slot
    WallClock::time_point t_now = WallClock::now();
    WallClock::time_point t_next_slot;
    size_t                next_slotidx;

    can_transmit_ = findNextSlot(t_now, t_next_slot, next_slotidx);
}
