#include "Logger.hh"
#include "SlottedMAC.hh"
#include "Util.hh"

SlottedMAC::SlottedMAC(std::shared_ptr<USRP> usrp,
                       std::shared_ptr<PHY> phy,
                       std::shared_ptr<Controller> controller,
                       std::shared_ptr<SnapshotCollector> collector,
                       std::shared_ptr<Channelizer> channelizer,
                       std::shared_ptr<Synthesizer> synthesizer,
                       double slot_size,
                       double guard_size,
                       double slot_modulate_lead_time,
                       double slot_send_lead_time)
  : MAC(usrp,
        phy,
        controller,
        collector,
        channelizer,
        synthesizer)
  , slot_size_(slot_size)
  , guard_size_(guard_size)
  , slot_modulate_lead_time_(slot_modulate_lead_time)
  , slot_send_lead_time_(slot_send_lead_time)
  , rx_slot_samps_(0)
  , rx_bufsize_(0)
  , tx_slot_samps_(0)
  , tx_full_slot_samps_(0)
  , stop_burst_(false)
  , logger_(logger)
  , done_(false)
{
}

SlottedMAC::~SlottedMAC()
{
}

void SlottedMAC::reconfigure(void)
{
    MAC::reconfigure();

    rx_slot_samps_ = rx_rate_*slot_size_;
    rx_bufsize_ = usrp_->getRecommendedBurstRXSize(rx_slot_samps_);
    tx_slot_samps_ = tx_rate_*(slot_size_ - guard_size_);
    tx_full_slot_samps_ = tx_rate_*slot_size_;

    if (usrp_->getTXRate() == usrp_->getRXRate())
        tx_fc_off_ = std::nullopt;
    else
        tx_fc_off_ = usrp_->getTXFrequency() - usrp_->getRXFrequency();

    synthesizer_->setMaxPacketSize(tx_slot_samps_);
}

void SlottedMAC::rxWorker(void)
{
    Clock::time_point t_now;        // Current time
    Clock::time_point t_cur_slot;   // Time at which current slot starts
    Clock::time_point t_next_slot;  // Time at which next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)
    unsigned          seq = 0;      // Current IQ buffer sequence number

    makeThisThreadHighPriority();

    while (!done_) {
        // Wait for slot size to be known
        if (rx_slot_samps_ == 0) {
            doze(100e-3);
            continue;
        }

        // Set up streaming starting at *next* slot
        t_now = Clock::now();
        t_slot_pos = fmod(t_now, slot_size_);
        t_next_slot = t_now + slot_size_ - t_slot_pos;

        // Bump the sequence number to indicate a discontinuity
        seq++;

        usrp_->startRXStream(Clock::to_mono_time(t_next_slot));

        while (!done_) {
            // Update times
            t_now = Clock::now();
            t_cur_slot = t_next_slot;
            t_next_slot += slot_size_;

            // Create buffer for slot
            auto curSlot = std::make_shared<IQBuf>(rx_bufsize_);

            curSlot->seq = seq++;

            // Push the buffer if we're snapshotting
            bool do_snapshot;

            if (snapshot_collector_)
                do_snapshot = snapshot_collector_->push(curSlot);
            else
                do_snapshot = false;

            // Put the buffer into the channelizer's queue so it can start
            // working now
            channelizer_->push(curSlot);

            // Read samples for current slot. The demodulator will do its thing
            // as we continue to read samples.
            bool ok = usrp_->burstRX(Clock::to_mono_time(t_cur_slot), rx_slot_samps_, *curSlot);

            // Update snapshot offset by finalizing this snapshot slot
            if (do_snapshot)
                snapshot_collector_->finalizePush();

            // If there was an RX error, break and set up the RX stream again.
            if (!ok)
                break;
        }

        usrp_->stopRXStream();
    }
}

void SlottedMAC::modulateSlot(slot_queue &q,
                              Clock::time_point when,
                              size_t prev_overfill,
                              size_t slotidx)
{
    assert(prev_overfill <= tx_slot_samps_);
    assert(prev_overfill <= tx_full_slot_samps_);

    auto slot = std::make_shared<Synthesizer::Slot>(when,
                                                    prev_overfill,
                                                    tx_slot_samps_ - prev_overfill,
                                                    tx_full_slot_samps_ - prev_overfill,
                                                    slotidx,
                                                    schedule_.size());

    // Tell the synthesizer to synthesize for this slot
    synthesizer_->modulate(slot);

    q.emplace(std::move(slot));
}

std::shared_ptr<Synthesizer::Slot> SlottedMAC::finalizeSlot(slot_queue &q,
                                                            Clock::time_point when)
{
    std::shared_ptr<Synthesizer::Slot> slot;
    Clock::time_point                  deadline;

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
            std::lock_guard<spinlock_mutex> lock(slot->mutex);

            slot->closed.store(true, std::memory_order_relaxed);
        }

        // Finalize the slot
        synthesizer_->finalize(*slot);

        // If the slot's deadline has passed, try the next slot. Otherwise,
        // return the slot.
        if (approx(deadline, when)) {
            return slot;
        } else {
            logEvent("MAC: MISSED SLOT DEADLINE: desired slot=%f; slot=%f; now=%f",
                (double) when.get_real_secs(),
                (double) deadline.get_real_secs(),
                (double) Clock::now().get_real_secs());

            // Stop any current TX burst.
            stop_burst_.store(true, std::memory_order_relaxed);

            // Re-queue packets that were modulated for this slot
            missedSlot(*slot);
        }
    }
}

void SlottedMAC::txWorker(void)
{
    std::shared_ptr<Synthesizer::Slot> slot;
    bool                               next_slot_start_of_burst = true;

    makeThisThreadHighPriority();

    while (!done_) {
        if (tx_slots_.size() == 0)
            continue;

        // Get a slot
        slot = std::move(tx_slots_.front());
        tx_slots_.pop();

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

        usrp_->burstTX(Clock::to_mono_time(slot->deadline) + slot->deadline_delay/tx_rate_,
                       next_slot_start_of_burst,
                       end_of_burst,
                       slot->iqbufs);

        next_slot_start_of_burst = end_of_burst;

        // Hand-off slot to TX notification thread
        {
            std::lock_guard<std::mutex> lock(txed_slots_mutex_);

            txed_slots_q_.emplace(std::move(slot));
        }

        txed_slots_cond_.notify_one();
    }
}

void SlottedMAC::txNotifier(void)
{
    std::shared_ptr<Synthesizer::Slot> slot;

    while (!done_) {
        // Get a slot
        {
            std::unique_lock<std::mutex> lock(txed_slots_mutex_);

            txed_slots_cond_.wait(lock, [this]{ return done_ || !txed_slots_q_.empty(); });

            // If we're done, we're done
            if (done_)
                return;

            slot = std::move(txed_slots_q_.front());
            txed_slots_q_.pop();
        }

        // Record the slot's load
        {
            std::lock_guard<spinlock_mutex> lock(load_mutex_);
            size_t                          nchannels = std::min(load_.nsamples.size(), slot->load.size());

            for (size_t i = 0; i < nchannels; ++i)
                load_.nsamples[i] += slot->load[i];

            load_.end = slot->deadline + (slot->deadline_delay + slot->nsamples)/tx_rate_;
        }

        // Log the transmissions
        if (logger_ && logger_->getCollectSource(Logger::kSentPackets)) {
            std::shared_ptr<IQBuf> &first = slot->iqbufs.front();

            // Log the sent packets
            for (auto it = slot->mpkts.begin(); it != slot->mpkts.end(); ++it) {
                Header hdr;

                hdr.curhop = (*it)->pkt->hdr.curhop;
                hdr.nexthop = (*it)->pkt->hdr.nexthop;
                hdr.seq = (*it)->pkt->hdr.seq;

                std::shared_ptr<IQBuf> &samples = (*it)->samples ? (*it)->samples : first;

                logger_->logSend(Clock::to_wall_time(samples->timestamp),
                                 hdr,
                                 (*it)->pkt->ehdr().src,
                                 (*it)->pkt->ehdr().dest,
                                 (*it)->pkt->tx_params->mcs.check,
                                 (*it)->pkt->tx_params->mcs.fec0,
                                 (*it)->pkt->tx_params->mcs.fec1,
                                 (*it)->pkt->tx_params->mcs.ms,
                                 tx_fc_off_ ? *tx_fc_off_ : (*it)->channel.fc,
                                 tx_rate_,
                                 (*it)->pkt->size(),
                                 samples,
                                 (*it)->offset,
                                 (*it)->nsamples);
            }
        }

        // Inform the controller of the transmission
        controller_->transmitted(*slot);

        // Tell the snapshot collector about local self-transmissions
        if (snapshot_collector_) {
            for (auto it = slot->mpkts.begin(); it != slot->mpkts.end(); ++it)
                snapshot_collector_->selfTX(Clock::to_mono_time(slot->deadline) + (*it)->start/tx_rate_,
                                            rx_rate_,
                                            tx_rate_,
                                            (*it)->channel.bw,
                                            (*it)->nsamples,
                                            tx_fc_off_ ? *tx_fc_off_ : (*it)->channel.fc);
        }
    }
}

void SlottedMAC::missedSlot(Synthesizer::Slot &slot)
{
    std::lock_guard<spinlock_mutex> lock(slot.mutex);

    // Close the slot
    slot.closed.store(true, std::memory_order_relaxed);

    // Re-queue packets that were modulated for this slot
    for (auto it = slot.mpkts.begin(); it != slot.mpkts.end(); ++it) {
        if (!(*it)->pkt->internal_flags.is_timestamp)
            controller_->missed(std::move((*it)->pkt));
    }
}

void SlottedMAC::missedRemainingSlots(slot_queue &q)
{
    while (!q.empty()) {
        missedSlot(*q.front());
        q.pop();
    }
}
