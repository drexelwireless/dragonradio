#include "Clock.hh"
#include "USRP.hh"
#include "Util.hh"
#include "mac/SlottedALOHA.hh"

SlottedALOHA::SlottedALOHA(std::shared_ptr<USRP> usrp,
                           std::shared_ptr<PHY> phy,
                           std::shared_ptr<Controller> controller,
                           std::shared_ptr<SnapshotCollector> collector,
                           std::shared_ptr<Channelizer> channelizer,
                           std::shared_ptr<Synthesizer> synthesizer,
                           double slot_size,
                           double guard_size,
                           double slot_modulate_lead_time,
                           double slot_send_lead_time,
                           double p)
  : SlottedMAC(usrp,
               phy,
               controller,
               collector,
               channelizer,
               synthesizer,
               slot_size,
               guard_size,
               slot_modulate_lead_time,
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
    tx_notifier_thread_ = std::thread(&SlottedALOHA::txNotifier, this);
}

SlottedALOHA::~SlottedALOHA()
{
    stop();
}

void SlottedALOHA::stop(void)
{
    done_ = true;

    txed_slots_cond_.notify_all();

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();

    if (tx_notifier_thread_.joinable())
        tx_notifier_thread_.join();
}

void SlottedALOHA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    if (schedule_.size() == 0 || slotidx_ >= schedule_[0].size())
        slotidx_ = 0;
}

void SlottedALOHA::txWorker(void)
{
    slot_queue        q;
    Clock::time_point t_now;            // Current time
    Clock::time_point t_next_slot;      // Time at which our next slot starts
    Clock::time_point t_following_slot; // Time at which the following slot starts
    double            t_slot_pos;       // Offset into the current slot (sec)

    makeThisThreadHighPriority();

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();
        t_slot_pos = fmod(t_now, slot_size_);
        t_next_slot = t_now + (slot_size_ - t_slot_pos);
        t_following_slot = t_next_slot + slot_size_;

        // Finalize next slot
        auto slot = finalizeSlot(q, t_next_slot);

        // Modulate following slot with probability p_
        if (dist_(gen_) < p_)
            modulateSlot(q, t_following_slot, 0, slotidx_);

        // Transmit next slot
        if (slot)
            txSlot(std::move(slot));

        // Sleep until TX time for following slot
        double delta;

        t_now = Clock::now();
        delta = (t_following_slot - t_now).get_real_secs() - slot_send_lead_time_;
        if (delta > 0.0)
            doze(delta);
    }

    missedRemainingSlots(q);
}
