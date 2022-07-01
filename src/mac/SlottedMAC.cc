// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <chrono>

#include "logging.hh"
#include "Logger.hh"
#include "SlottedMAC.hh"
#include "liquid/Modem.hh"
#include "util/threads.hh"

using namespace std::literals::chrono_literals;

using Slot = SlotSynthesizer::Slot;

template <class Clock, class Duration, class Rep, class Period>
bool within(const std::chrono::time_point<Clock,Duration>& t1,
            const std::chrono::time_point<Clock,Duration>& t2,
            const std::chrono::duration<Rep,Period>& diff)
{
    return std::chrono::abs(std::chrono::duration<double>(t2-t1)) < diff;
}

SlottedMAC::SlottedMAC(std::shared_ptr<Radio> radio,
                       std::shared_ptr<Controller> controller,
                       std::shared_ptr<SnapshotCollector> collector,
                       std::shared_ptr<Channelizer> channelizer,
                       std::shared_ptr<SlotSynthesizer> synthesizer,
                       double rx_period,
                       double slot_send_lead_time,
                       unsigned nsyncthreads)
  : MAC(radio,
        controller,
        collector,
        channelizer,
        synthesizer,
        rx_period,
        nsyncthreads)
  , slot_synthesizer_(synthesizer)
  , slot_send_lead_time_(slot_send_lead_time)
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
    tx_slot_.disable();
}

void SlottedMAC::modulateSlot(WallClock::time_point when,
                              size_t prev_overfill,
                              size_t slotidx)
{
    assert(prev_overfill <= tx_full_slot_samps_);

    next_slot_ = std::make_shared<Slot>(when,
                                        prev_overfill,
                                        tx_slot_samps_ - prev_overfill,
                                        tx_full_slot_samps_ - prev_overfill,
                                        slotidx,
                                        schedule_.nchannels());

    // Tell the synthesizer to synthesize for this slot
    slot_synthesizer_->modulate(next_slot_);
}

std::shared_ptr<Slot> SlottedMAC::finalizeSlot(WallClock::time_point when)
{
    std::shared_ptr<Slot> slot;
    WallClock::time_point deadline;

    for (;;) {
        // Get the next slot
        {
            // If we don't have any slots synthesized, we can't send anything
            if (!next_slot_)
                return nullptr;

            // Check deadline of next slot
            deadline = next_slot_->deadline;

            // If the next slot needs to be transmitted or tossed, pop it,
            // otherwise return nullptr since we need to wait longer
            if (deadline < when || within(deadline, when, 1us))
                slot = std::move(next_slot_);
            else
                return nullptr;
        }

        // Close the slot. We grab the slot's mutex to guarantee that all
        // synthesizer threads have seen that the slot is closed---this serves
        // as a barrier. After this, no synthesizer will touch the slot, so we
        // are guaranteed exclusive access.
        {
            std::lock_guard<std::mutex> lock(slot->mutex);

            slot->closed.store(true, std::memory_order_release);
        }

        // Finalize the slot
        slot_synthesizer_->finalize(*slot);

        // If the slot's deadline has passed, try the next slot. Otherwise,
        // return the slot.
        if (within(deadline, when, 1us)) {
            return slot;
        } else {
            logMAC(LOGWARNING, "MISSED SLOT DEADLINE: desired slot=%f; slot=%f; now=%f",
                (double) when.time_since_epoch().count(),
                (double) deadline.time_since_epoch().count(),
                (double) WallClock::now().time_since_epoch().count());

            // Stop any current TX burst.
            stop_burst_.store(true, std::memory_order_relaxed);

            // Re-queue packets that were modulated for this slot
            missedSlot(*slot);
        }
    }
}

void SlottedMAC::txWorker(void)
{
    std::shared_ptr<Slot> slot;
    bool                  next_slot_start_of_burst = true;

    for (;;) {
        // Get a slot
        if (!tx_slot_.pop(slot)) {
            // Synchronize on state change
            if (needs_sync()) {
                sync();

                if (done_)
                    return;
            }

            continue;
        }

        // If the slot doesn't contain any IQ data to send, we're done
        if (slot->mpkts.empty()) {
            if (!next_slot_start_of_burst) {
                radio_->stopTXBurst();
                next_slot_start_of_burst = true;
            }

            // Log the TX record
            if (logger_)
                logger_->logTXRecord(WallClock::to_mono_time(slot->deadline), slot->nsamples, tx_rate_);

            continue;
        }

        if (stop_burst_.load(std::memory_order_relaxed)) {
            stop_burst_.store(false, std::memory_order_relaxed);

            radio_->stopTXBurst();
            next_slot_start_of_burst = true;
        }

        // Transmit the packets via the radio
        bool end_of_burst = slot->length() < slot->full_slot_samples;

        radio_->burstTX(WallClock::to_mono_time(slot->deadline) + WallClock::duration(slot->deadline_delay/tx_rate_),
                        next_slot_start_of_burst,
                        end_of_burst,
                        slot->iqbufs);

        next_slot_start_of_burst = end_of_burst;

        // Hand-off TX record to TX notification thread
        {
            std::lock_guard<std::mutex> lock(tx_records_mutex_);

            tx_records_.emplace(WallClock::to_mono_time(slot->deadline),
                                slot->deadline_delay,
                                slot->nsamples,
                                std::move(slot->iqbufs),
                                std::move(slot->mpkts));
        }

        tx_records_cond_.notify_one();
    }
}

void SlottedMAC::missedSlot(Slot &slot)
{
    std::lock_guard<std::mutex> lock(slot.mutex);

    // Close the slot
    slot.closed.store(true, std::memory_order_release);

    // Re-queue packets that were modulated for this slot
    for (auto it = slot.mpkts.begin(); it != slot.mpkts.end(); ++it)
        controller_->missed(std::move((*it)->pkt));
}

void SlottedMAC::wake_dependents()
{
    MAC::wake_dependents();

    // Disable the TX slot queue
    tx_slot_.disable();
}

void SlottedMAC::reconfigure(void)
{
    MAC::reconfigure();

    const auto slot_size = schedule_.getSlotSize();
    const auto guard_size = schedule_.getGuardSize();

    tx_slot_samps_ = tx_rate_*(slot_size - guard_size).count();
    tx_full_slot_samps_ = tx_rate_*slot_size.count();

    // Re-enable the TX slot queue
    tx_slot_.enable();
}
