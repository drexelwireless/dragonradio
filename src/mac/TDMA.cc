// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "USRP.hh"
#include "mac/TDMA.hh"
#include "util/threads.hh"

TDMA::TDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<Controller> controller,
           std::shared_ptr<SnapshotCollector> collector,
           std::shared_ptr<Channelizer> channelizer,
           std::shared_ptr<SlotSynthesizer> synthesizer,
           double slot_size,
           double guard_size,
           double slot_send_lead_time,
           size_t nslots)
  : SlottedMAC(usrp,
               controller,
               collector,
               channelizer,
               synthesizer,
               slot_size,
               guard_size,
               slot_send_lead_time)
  , frame_size_(nslots*slot_size_)
  , nslots_(nslots)
  , tdma_schedule_(nslots)
{
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

void TDMA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    for (size_t i = 0; i < nslots_; ++i)
        tdma_schedule_[i] = schedule_.canTransmitInSlot(i);

    frame_size_ = nslots_*slot_size_;

    // Determine whether or not we have a slot
    WallClock::time_point t_now = WallClock::now();
    WallClock::time_point t_next_slot;
    size_t                next_slotidx;

    can_transmit_ = findNextSlot(t_now, t_next_slot, next_slotidx);
}

bool TDMA::isFDMA(void) const
{
    return schedule_.isFDMA();
}

void TDMA::txSlotWorker(void)
{
    slot_queue            q;
    WallClock::time_point t_now;              // Current time
    WallClock::time_point t_next_slot;        // Time at which our next slot starts
    WallClock::time_point t_following_slot;   // Time at which our following slot starts
    size_t                next_slotidx;       // Slot index of next slot
    size_t                following_slotidx;  // Slot index of following slot
    size_t                noverfill = 0;      // Number of overfilled samples
    size_t                noverfillslots = 0; // Number of overfilled slots

    while (!done_) {
        t_now = WallClock::now();

        // If we missed a slot, find the next slot
        if (t_now > t_next_slot) {
            if (!findNextSlot(t_now, t_next_slot, next_slotidx)) {
                logMAC(LOGDEBUG, "NO SLOT");
                // Sleep for 100ms if we don't yet have a slot
                doze(100e-3);
                continue;
            }
        }

        // If we're less that one slot away from our next slot, finalize it
        // and transmit it
        if ((t_now - t_next_slot).get_real_secs() < slot_size_) {
            // Finalize next slot. After this returns, we have EXCLUSIVE access
            // to the slot.
            auto slot = finalizeSlot(q, t_next_slot);

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

            // Find following slot. We divide slot_size_ by two to avoid
            // possible rounding issues where we mights end up skipping a slot.
            // If we reach this line of code, then findNextSlot must have
            // returned true above, in which case we know it will return true
            // here, so we do not need to check the result.
            (void) findNextSlot(t_next_slot + noverfillslots*slot_size_ + slot_size_/2.0,
                                t_following_slot,
                                following_slotidx);

            // Schedule modulation of following slot
            modulateSlot(q,
                         t_following_slot,
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
        double delta;

        t_now = WallClock::now();
        delta = (t_next_slot - t_now).get_real_secs() - slot_send_lead_time_;
        if (delta > 0.0)
            doze(delta);
    }

    missedRemainingSlots(q);
}

bool TDMA::findNextSlot(WallClock::time_point t,
                        WallClock::time_point &t_next,
                        size_t &next_slotidx)
{
    double t_slot_pos; // Offset into the current slot (sec)
    size_t cur_slot;   // Current slot index
    size_t tx_slot;    // Slots before we can TX

    t_slot_pos = fmod(t, slot_size_);
    cur_slot = fmod(t, frame_size_) / slot_size_;

    for (tx_slot = 1; tx_slot <= nslots_; ++tx_slot) {
        if (tdma_schedule_[(cur_slot + tx_slot) % nslots_]) {
            t_next = t + (tx_slot*slot_size_ - t_slot_pos);
            next_slotidx = (cur_slot + tx_slot) % nslots_;
            return true;
        }
    }

    return false;
}
