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
  : MAC(radio,
        controller,
        collector,
        channelizer,
        synthesizer,
        rx_period,
        5)
  , tx_slot_samps_(0)
  , tx_full_slot_samps_(0)
  , stop_burst_(false)
  , slotidx_(0)
  , p_(p)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
  , arrival_dist_(p)
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

void SlottedALOHA::stop(void)
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

void SlottedALOHA::txWorker(void)
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

void SlottedALOHA::txSlotWorker(void)
{
    WallClock::time_point t_now;              // Current time
    WallClock::time_point t_next_slot;        // Time at which our next slot starts
    WallClock::time_point t_following_slot;   // Time at which the following slot starts
    WallClock::duration   t_slot_pos;         // Offset into the current slot (sec)
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
            findNextSlot(t_now, t_next_slot);

        // If we're less that one slot away from our next slot, pop and transmit
        // it
        auto slot_size = schedule_.getSlotSize();

        if (t_next_slot - t_now < slot_size) {
            auto slot = synthesizer_->pop_slot();

            if (slot.txrecord.nsamples > 0 && slot.txrecord.timestamp != WallClock::to_mono_time(t_next_slot)) {
                logMAC(LOGWARNING, "MISSED SLOT DEADLINE: desired slot=%f; slot=%f; now=%f",
                    (double) t_next_slot.time_since_epoch().count(),
                    (double) WallClock::to_wall_time(*slot.txrecord.timestamp).time_since_epoch().count(),
                    (double) WallClock::now().time_since_epoch().count());

                // Stop any current TX burst.
                stop_burst_.store(true, std::memory_order_relaxed);

                // Re-queue packets that were modulated for this slot
                abortTXRecord(slot.txrecord);

                continue;
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
            assert(findNextSlot(t_next_slot + noverfillslots*slot_size + slot_size/2.0,
                                t_following_slot));

            // Modulate following slot with probability p_
            if (dist_(gen_) < p_)
                synthesizer_->push_slot(t_following_slot, getSlotIndex(), noverfill);

            // Transmit next slot
            if (slot.txrecord.nsamples > 0)
                tx_slot_ = std::move(slot);

            // The following slot is now the next slot
            t_next_slot = t_following_slot;
        }

        // Sleep until it's time to send the next slot
        sleep_for((t_next_slot - WallClock::now()) - radio_->getTXLeadTime());
    }

    // We cannot transmit remaining packets
    TXRecord txrecord = synthesizer_->try_pop();

    abortTXRecord(txrecord);
}

bool SlottedALOHA::findNextSlot(WallClock::time_point t,
                                WallClock::time_point& t_next)
{
    WallClock::duration t_slot_pos; // Offset into the current slot (sec)
    const auto          slot_size = schedule_.getSlotSize();

    t_slot_pos = schedule_.slotOffsetAt(t);

    t_next = t + (slot_size - t_slot_pos);

    return true;
}

void SlottedALOHA::wake_dependents(void)
{
    MAC::wake_dependents();

    // Disable the TX slot queue
    tx_slot_.disable();

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }
}

void SlottedALOHA::reconfigure(void)
{
    MAC::reconfigure();

    const auto slot_size = schedule_.getSlotSize();
    const auto guard_size = schedule_.getGuardSize();

    tx_slot_samps_ = tx_rate_*(slot_size - guard_size).count();
    tx_full_slot_samps_ = tx_rate_*slot_size.count();

    // We can always transmit
    can_transmit_ = true;

    // Re-enable the TX slot queue
    tx_slot_.enable();

    // Update slot index
    if (schedule_.nchannels() == 0 || slotidx_ >= schedule_.nslots())
        slotidx_ = 0;
}
