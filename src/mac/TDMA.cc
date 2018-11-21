#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "USRP.hh"
#include "RadioConfig.hh"
#include "Util.hh"
#include "mac/TDMA.hh"

TDMA::TDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<PHY> phy,
           std::shared_ptr<SnapshotCollector> collector,
           const Channels &rx_channels,
           const Channels &tx_channels,
           std::shared_ptr<PacketModulator> modulator,
           std::shared_ptr<PacketDemodulator> demodulator,
           double slot_size,
           double guard_size,
           double demod_overlap_size,
           size_t nslots)
  : SlottedMAC(usrp, phy, collector, rx_channels, tx_channels, modulator, demodulator, slot_size, guard_size, demod_overlap_size)
  , slots_(*this, nslots)
  , superslots_(false)
{
    rx_thread_ = std::thread(&TDMA::rxWorker, this);
    tx_thread_ = std::thread(&TDMA::txWorker, this);
}

TDMA::~TDMA()
{
    stop();
}

void TDMA::stop(void)
{
    done_ = true;

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();
}

void TDMA::reconfigure(void)
{
    SlottedMAC::reconfigure();

    frame_size_ = slot_size_*slots_.size();
}

void TDMA::sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt)
{
    Clock::time_point t_next_slot;
    bool              own_next_slot;

    findNextSlot(t, t_next_slot, own_next_slot);

    timestampPacket(t_next_slot, std::move(pkt));
}

void TDMA::txWorker(void)
{
    Clock::time_point t_now;            // Current time
    Clock::time_point t_prev_slot;      // Previous, completed slot
    Clock::time_point t_next_slot;      // Time at which our next slot starts
    Clock::time_point t_following_slot; // Time at which our following slot starts
    bool              own_next_slot;    // Do we own the next slot too?
    size_t            noverfill = 0;    // Number of overfilled samples;
    double            delta;            // Time until we need to wake up to modulate more packets

    uhd::set_thread_priority_safe();

    t_prev_slot = Clock::time_point { 0.0 };

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();

        if (!findNextSlot(t_now, t_next_slot, own_next_slot)) {
            // Sleep for 100ms if we don't yet have a slot
            doze(100e-3);
            continue;
        }

        // Find following slot. We divide slot_size_ by two to avoid possible
        // rounding issues where we mights end up skipping a slot.
        findNextSlot(t_next_slot + slot_size_/2.0, t_following_slot, own_next_slot);

        // Schedule transmission for start of our next slot if we haven't
        // already transmitted for that slot
        if (!approx(t_next_slot, t_prev_slot)) {
            if (superslots_ && own_next_slot)
                noverfill = txSlot(t_next_slot + noverfill/tx_rate_, tx_full_slot_samps_ - noverfill, true);
            else
                noverfill = txSlot(t_next_slot + noverfill/tx_rate_, tx_slot_samps_ - noverfill, false);

            // XXX For some reason this is necessary to please USRP. Otherwise
            // we get late TX errors.
            if (noverfill != 0)
                noverfill += 1;

            t_prev_slot = t_next_slot;
        }

        // Sleep until it's time to modulate the next slot
        t_now = Clock::now();

        delta = (t_following_slot - t_now - rc.slot_modulate_time).get_real_secs();

        if (delta > 0.0)
            doze(delta);

        // Modulate samples for next slot
        modulator_->modulate(premod_samps_);

        // Sleep until it's time to send the slot's modulated data
        t_now = Clock::now();

        delta = (t_following_slot - t_now - rc.slot_send_time).get_real_secs();

        if (delta > 0.0)
            doze(delta);
    }
}

bool TDMA::findNextSlot(Clock::time_point t,
                        Clock::time_point &t_next,
                        bool &owns_next_slot)
{
    double t_slot_pos; // Offset into the current slot (sec)
    size_t cur_slot;   // Current slot index
    size_t tx_slot;    // Slots before we can TX

    t_slot_pos = fmod(t, slot_size_);
    cur_slot = fmod(t, frame_size_) / slot_size_;

    for (tx_slot = 1; tx_slot <= slots_.size(); ++tx_slot) {
        if (slots_[(cur_slot + tx_slot) % slots_.size()]) {
            t_next = t + (tx_slot*slot_size_ - t_slot_pos);
            owns_next_slot = slots_[(cur_slot + tx_slot + 1) % slots_.size()];
            return true;
        }
    }

    return false;
}
