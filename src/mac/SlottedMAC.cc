#include <uhd/utils/thread_priority.hpp>

#include "Logger.hh"
#include "SlottedMAC.hh"

SlottedMAC::SlottedMAC(std::shared_ptr<USRP> usrp,
                       std::shared_ptr<PHY> phy,
                       std::shared_ptr<Controller> controller,
                       std::shared_ptr<SnapshotCollector> collector,
                       const Channels &rx_channels,
                       const Channels &tx_channels,
                       std::shared_ptr<PacketModulator> modulator,
                       std::shared_ptr<PacketDemodulator> demodulator,
                       double slot_size,
                       double guard_size,
                       double demod_overlap_size)
  : MAC(usrp, phy, controller, collector, rx_channels, tx_channels, modulator, demodulator)
  , slot_size_(slot_size)
  , guard_size_(guard_size)
  , demod_overlap_size_(demod_overlap_size)
  , premod_slots_(1.0)
  , logger_(logger)
  , done_(false)
{
}

SlottedMAC::~SlottedMAC()
{
}

double SlottedMAC::getSlotSize(void)
{
    return slot_size_;
}

void SlottedMAC::setSlotSize(double t)
{
    slot_size_ = t;
    reconfigure();
}

double SlottedMAC::getGuardSize(void)
{
    return guard_size_;
}

void SlottedMAC::setGuardSize(double t)
{
    guard_size_ = t;
    reconfigure();
}

double SlottedMAC::getDemodOverlapSize(void)
{
    return demod_overlap_size_;
}

void SlottedMAC::setDemodOverlapSize(double t)
{
    demod_overlap_size_ = t;
    reconfigure();
}

double SlottedMAC::getPreModulateSlots(void)
{
    return premod_slots_;
}

void SlottedMAC::setPreModulateSlots(double n)
{
    premod_slots_ = n;
    reconfigure();
}

void SlottedMAC::reconfigure(void)
{
    MAC::reconfigure();

    rx_slot_samps_ = rx_rate_*slot_size_;
    tx_slot_samps_ = tx_rate_*(slot_size_ - guard_size_);
    tx_full_slot_samps_ = tx_rate_*slot_size_;
    premod_samps_ = premod_slots_*tx_full_slot_samps_;

    modulator_->setMaxPacketSize(tx_slot_samps_);

    demodulator_->setWindowParameters(demod_overlap_size_*rx_rate_,
                                      (slot_size_ - guard_size_ + demod_overlap_size_)*rx_rate_);
}

void SlottedMAC::rxWorker(void)
{
    Clock::time_point t_now;        // Current time
    Clock::time_point t_cur_slot;   // Time at which current slot starts
    Clock::time_point t_next_slot;  // Time at which next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Set up streaming starting at *next* slot
        t_now = Clock::now();
        t_slot_pos = fmod(t_now, slot_size_);
        t_next_slot = t_now + slot_size_ - t_slot_pos;

        usrp_->startRXStream(Clock::to_mono_time(t_next_slot));

        while (!done_) {
            // Update times
            t_now = Clock::now();
            t_cur_slot = t_next_slot;
            t_next_slot += slot_size_;

            // Create buffer for slot
            auto curSlot = std::make_shared<IQBuf>(rx_slot_samps_ + usrp_->getMaxRXSamps());

            // Push the buffer if we're snapshotting
            bool do_snapshot;

            if (snapshot_collector_)
                do_snapshot = snapshot_collector_->push(curSlot);
            else
                do_snapshot = false;

            // Put the buffer into the demodulator's queue so it can start
            // working now
            demodulator_->push(curSlot);

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

size_t SlottedMAC::txSlot(Clock::time_point when, size_t maxSamples, bool overfill)
{
    std::list<std::shared_ptr<IQBuf>>     txBuf;  // IQ buffers to transmit
    std::list<std::unique_ptr<ModPacket>> modBuf; // Modulated packets

    // Put the timestamped packet on the front of the TX queue if it is due to
    // be sent this slot.
    {
        std::lock_guard<spinlock_mutex> lock(timestamped_mutex_);

        if (timestamped_mpkt_) {
            if (approx(timestamped_deadline_, when)) {
                maxSamples -= timestamped_mpkt_->samples->size();
                modBuf.emplace_back(std::move(timestamped_mpkt_));
            } else if (timestamped_deadline_ < when) {
                logEvent("TIMESYNC: MISSED TIMESTAMPED PACKET: timestamp=%f; slot=%f",
                    (double) timestamped_deadline_.get_real_secs(),
                    (double) when.get_real_secs());

                timestamped_mpkt_.reset();
            }
        }
    }

    // Fill the rest of the slot with modulated packets
    size_t nsamples;

    nsamples = modulator_->pop(modBuf, maxSamples, overfill);

    if (!modBuf.empty()) {
        // Transmit the packets via the USRP
        if (logger_ && logger_->getCollectSource(Logger::kSentPackets)) {
            for (auto it = modBuf.begin(); it != modBuf.end(); ++it)
                txBuf.emplace_back((*it)->samples);

            usrp_->burstTX(Clock::to_mono_time(when), txBuf);

            // Log the sent packets
            for (auto it = modBuf.begin(); it != modBuf.end(); ++it) {
                Header hdr;

                hdr.curhop = (*it)->pkt->curhop;
                hdr.nexthop = (*it)->pkt->nexthop;
                hdr.seq = (*it)->pkt->seq;

                logger_->logSend((*it)->samples->timestamp,
                                 hdr,
                                 (*it)->pkt->src,
                                 (*it)->pkt->dest,
                                 (*it)->pkt->tx_params->mcs.check,
                                 (*it)->pkt->tx_params->mcs.fec0,
                                 (*it)->pkt->tx_params->mcs.fec1,
                                 (*it)->pkt->tx_params->mcs.ms,
                                 (*it)->fc,
                                 tx_rate_,
                                 (*it)->pkt->size(),
                                 (*it)->samples);
            }
        } else {
            // When we aren't logging, use std::move to hand the samples over
            // without retaining a reference. This avoids a shared_ptr refcount
            // operation. Do we care?
            for (auto it = modBuf.begin(); it != modBuf.end(); ++it)
                txBuf.emplace_back(std::move((*it)->samples));

            usrp_->burstTX(Clock::to_mono_time(when), txBuf);
        }

        // Inform the controller of the transmission
        for (auto it = modBuf.begin(); it != modBuf.end(); ++it)
            controller_->transmitted((*it)->pkt);

        // Tell the snapshot collector about this local self-transmission
        if (snapshot_collector_)
            snapshot_collector_->selfTX(when,
                                        rx_rate_,
                                        tx_rate_,
                                        demodulator_->getChannelRate(),
                                        nsamples,
                                        getTXShift());
    }

    return (nsamples > maxSamples) ? (nsamples - maxSamples) : 0;
}
