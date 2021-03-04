// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Logger.hh"
#include "SlottedMAC.hh"
#include "liquid/Modem.hh"
#include "util/threads.hh"

using Slot = SlotSynthesizer::Slot;

SlottedMAC::SlottedMAC(std::shared_ptr<USRP> usrp,
                       std::shared_ptr<PHY> phy,
                       std::shared_ptr<Controller> controller,
                       std::shared_ptr<SnapshotCollector> collector,
                       std::shared_ptr<Channelizer> channelizer,
                       std::shared_ptr<SlotSynthesizer> synthesizer,
                       double slot_size,
                       double guard_size,
                       double slot_send_lead_time)
  : MAC(usrp,
        phy,
        controller,
        collector,
        channelizer,
        synthesizer,
        slot_size)
  , slot_synthesizer_(synthesizer)
  , slot_size_(slot_size)
  , guard_size_(guard_size)
  , slot_send_lead_time_(slot_send_lead_time)
  , tx_slot_samps_(0)
  , tx_full_slot_samps_(0)
  , stop_burst_(false)
{
}

SlottedMAC::~SlottedMAC()
{
}

void SlottedMAC::reconfigure(void)
{
    MAC::reconfigure();

    tx_slot_samps_ = tx_rate_*(slot_size_ - guard_size_);
    tx_full_slot_samps_ = tx_rate_*slot_size_;

    // If this is an FDMA MAC, all MCS entries are fair game
    if (isFDMA()) {
        for (mcsidx_t mcsidx = 0; mcsidx < phy_->mcs_table.size(); ++mcsidx)
            phy_->mcs_table[mcsidx].valid = true;
    } else {
        // Compute the maximum number of samples that will fit in minimum-bandwidth
        // channel and use this to mark valid MCS indices.
        size_t max_samples = min_chan_bw_*(slot_size_ - guard_size_);

        for (mcsidx_t mcsidx = 0; mcsidx < phy_->mcs_table.size(); ++mcsidx)
            phy_->mcs_table[mcsidx].valid = phy_->getModulatedSize(mcsidx, controller_->getMTU()) <= max_samples;

        if (!phy_->mcs_table[phy_->mcs_table.size()-1].valid)
            logMAC(LOGWARNING, "WARNING: Slot size too small to support a full-sized packet!");
    }
}

void SlottedMAC::stop(void)
{
    done_ = true;

    tx_slots_.stop();
}

void SlottedMAC::modulateSlot(slot_queue &q,
                              WallClock::time_point when,
                              size_t prev_overfill,
                              size_t slotidx)
{
    assert(prev_overfill <= tx_full_slot_samps_);

    auto slot = std::make_shared<Slot>(when,
                                       prev_overfill,
                                       tx_slot_samps_ - prev_overfill,
                                       tx_full_slot_samps_ - prev_overfill,
                                       slotidx,
                                       schedule_.size());

    // Tell the synthesizer to synthesize for this slot
    slot_synthesizer_->modulate(slot);

    q.push(std::move(slot));
}

std::shared_ptr<Slot> SlottedMAC::finalizeSlot(slot_queue &q,
                                               WallClock::time_point when)
{
    std::shared_ptr<Slot> slot;
    WallClock::time_point deadline;

    for (;;) {
        // Get the next slot
        {
            // If we don't have any slots synthesized, we can't send anything
            if (q.empty())
                return nullptr;

            // Check deadline of next slot
            deadline = q.front()->deadline;

            // If the next slot needs to be transmitted or tossed, pop it,
            // otherwise return nullptr since we need to wait longer
            if (deadline < when || approx(deadline, when)) {
                slot = std::move(q.front());
                q.pop();
            } else
                return nullptr;
        }

        // Close the slot. We grab the slot's mutex to guarantee that all
        // synthesizer threads have seen that the slot is closed---this serves
        // as a barrier. After this, no synthesizer will touch the slot, so we
        // are guaranteed exclusive access.
        {
            std::lock_guard<std::mutex> lock(slot->mutex);

            slot->closed.store(true, std::memory_order_relaxed);
        }

        // Finalize the slot
        slot_synthesizer_->finalize(*slot);

        // If the slot's deadline has passed, try the next slot. Otherwise,
        // return the slot.
        if (approx(deadline, when)) {
            return slot;
        } else {
            logMAC(LOGWARNING, "MISSED SLOT DEADLINE: desired slot=%f; slot=%f; now=%f",
                (double) when.get_real_secs(),
                (double) deadline.get_real_secs(),
                (double) WallClock::now().get_real_secs());

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

    while (!done_) {
        // Get a slot
        if (!tx_slots_.pop(slot)) {
            if (done_)
                return;

            continue;
        }

        // If the slot doesn't contain any IQ data to send, we're done
        if (slot->mpkts.empty()) {
            if (!next_slot_start_of_burst) {
                usrp_->stopTXBurst();
                next_slot_start_of_burst = true;
            }

            continue;
        }

        if (stop_burst_.load(std::memory_order_relaxed)) {
            stop_burst_.store(false, std::memory_order_relaxed);

            usrp_->stopTXBurst();
            next_slot_start_of_burst = true;
        }

        // Transmit the packets via the USRP
        bool end_of_burst = slot->length() < slot->full_slot_samples;

        usrp_->burstTX(WallClock::to_mono_time(slot->deadline) + slot->deadline_delay/tx_rate_,
                       next_slot_start_of_burst,
                       end_of_burst,
                       slot->iqbufs);

        next_slot_start_of_burst = end_of_burst;

        // Hand-off TX record to TX notification thread
        {
            std::lock_guard<std::mutex> lock(tx_records_mutex_);

            tx_records_.push(TXRecord { WallClock::to_mono_time(slot->deadline), slot->deadline_delay, slot->nsamples, std::move(slot->iqbufs), std::move(slot->mpkts) });
        }

        tx_records_cond_.notify_one();
    }
}

void SlottedMAC::missedSlot(Slot &slot)
{
    std::lock_guard<std::mutex> lock(slot.mutex);

    // Close the slot
    slot.closed.store(true, std::memory_order_relaxed);

    // Re-queue packets that were modulated for this slot
    for (auto it = slot.mpkts.begin(); it != slot.mpkts.end(); ++it)
        controller_->missed(std::move((*it)->pkt));
}

void SlottedMAC::missedRemainingSlots(slot_queue &q)
{
    while (!q.empty()) {
        missedSlot(*q.front());
        q.pop();
    }
}
