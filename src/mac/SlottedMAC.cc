// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <chrono>

#include "logging.hh"
#include "Clock.hh"
#include "Radio.hh"
#include "mac/SlottedMAC.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

SlottedMAC::SlottedMAC(std::shared_ptr<Radio> radio,
                       std::shared_ptr<Controller> controller,
                       std::shared_ptr<SnapshotCollector> collector,
                       std::shared_ptr<Channelizer> channelizer,
                       std::shared_ptr<Synthesizer> synthesizer,
                       double rx_period,
                       unsigned nsyncthreads)
  : MAC(radio,
        controller,
        collector,
        channelizer,
        synthesizer,
        rx_period,
        nsyncthreads)
  , slot_send_lead_time_(5e-3)
  , tx_slot_samps_(0)
  , tx_full_slot_samps_(0)
  , stop_burst_(false)
{
}

SlottedMAC::~SlottedMAC()
{
}

void SlottedMAC::stop(void)
{
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

void SlottedMAC::txWorker(void)
{
    std::optional<TXSlot> slot;

    for (;;) {
        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;
        }

        if (!tx_slot_.pop(slot))
            continue;

        // If the slot doesn't contain any IQ data to send, we're done
        if (slot->txrecord.mpkts.empty()) {
            radio_->stopTXBurst();

            continue;
        }

        // If we were told to stop the TX burst, do so.
        if (stop_burst_.load(std::memory_order_relaxed)) {
            stop_burst_.store(false, std::memory_order_relaxed);

            radio_->stopTXBurst();
        }

        // Transmit the packets via the radio
        radio_->burstTX(*slot->txrecord.timestamp + WallClock::duration(slot->txrecord.delay/tx_rate_),
                        // Start burst if we're not already in one
                        !radio_->inTXBurst(),
                        // Stop burst if slot isn't continued or if we didn't
                        // fill this slot
                        !slot->continued || slot->nexcess < 0,
                        slot->txrecord.iqbufs);

        // Hand-off TX record to TX notification thread
        pushTXRecord(std::move(slot->txrecord));
    }
}

void SlottedMAC::txSlotWorker(void)
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

            // If we cannot transmit, sleep
            if (!can_transmit_) {
                sleep_until_state_change();
                continue;
            }
        }

        t_now = WallClock::now();

        // If we missed a slot, find the next slot
        if (t_now > t_next_slot)
            findNextSlot(t_now, t_next_slot, next_slotidx);

        // If we're less that one slot away from our next slot, finalize it
        // and transmit it
        auto slot_size = schedule_.getSlotSize();

        if (t_next_slot - t_now < slot_size) {
            auto slot = synthesizer_->pop_slot();

            if (slot.txrecord.nsamples > 0 && slot.deadline != t_next_slot) {
                logMAC(LOGWARNING, "MISSED SLOT DEADLINE: desired slot=%f; slot=%f; now=%f",
                    (double) t_next_slot.time_since_epoch().count(),
                    (double) slot.deadline.time_since_epoch().count(),
                    (double) WallClock::now().time_since_epoch().count());

                // Stop any current TX burst.
                stop_burst_.store(true, std::memory_order_relaxed);

                // Re-queue packets that were modulated for this slot
                abortTXRecord(slot.txrecord);

                slot.clear();
            }

            // Determine how many samples were sent beyond the end of the slot,
            // i.e., the number of overfilled samples.
            if (slot.txrecord.nsamples > 0 && slot.nexcess > 0) {
                noverfill = slot.nexcess % tx_full_slot_samps_;
                noverfillslots = slot.nexcess / tx_full_slot_samps_;
            } else {
                noverfill = 0;
                noverfillslots = 0;
            }

            // Find following slot. We divide slot_size by two to avoid possible
            // rounding issues where we mights end up skipping a slot. If we
            // reach this line of code, then findNextSlot must have returned
            // true above, in which case we know it will return true here, so we
            // do not need to check the result.
            findNextSlot(t_next_slot + noverfillslots*slot_size + slot_size/2.0,
                         t_following_slot,
                         following_slotidx);

            // Schedule modulation of following slot
            if (transmitInSlot(t_following_slot, following_slotidx))
                synthesizer_->push_slot(t_following_slot, following_slotidx, noverfill);

            // Transmit next slot
            if (slot.txrecord.nsamples > 0)
                tx_slot_ = std::move(slot);

            // The following slot is now the next slot
            t_next_slot = t_following_slot;
            next_slotidx = following_slotidx;
        }

        // Sleep until it's time to send the next slot
        sleep_for((t_next_slot - WallClock::now()) - slot_send_lead_time_);
    }

    // We cannot transmit remaining packets
    TXRecord txrecord = synthesizer_->try_pop();

    abortTXRecord(txrecord);
}

bool SlottedMAC::transmitInSlot(WallClock::time_point t,
                                size_t slotidx)
{
    return true;
}

void SlottedMAC::wake_dependents()
{
    // Disable the TX slot queue
    tx_slot_.disable();

    MAC::wake_dependents();
}

void SlottedMAC::reconfigure(void)
{
    MAC::reconfigure();

    // Determine number of samples in slot
    const auto slot_size = schedule_.getSlotSize();
    const auto guard_size = schedule_.getGuardSize();

    tx_slot_samps_ = tx_rate_*(slot_size - guard_size).count();
    tx_full_slot_samps_ = tx_rate_*slot_size.count();

    // Re-enable the TX slot queue
    tx_slot_.enable();
}
