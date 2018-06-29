#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "USRP.hh"
#include "Util.hh"
#include "mac/TDMA.hh"

TDMA::TDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<PHY> phy,
           std::shared_ptr<PacketModulator> modulator,
           std::shared_ptr<PacketDemodulator> demodulator,
           double bandwidth,
           double slot_size,
           double guard_size,
           size_t nslots)
  : SlottedMAC(usrp, phy, modulator, demodulator, bandwidth, slot_size, guard_size)
{
    slots_.resize(nslots, false);

    reconfigure();

    rx_thread_ = std::thread(&TDMA::rxWorker, this);
    tx_thread_ = std::thread(&TDMA::txWorker, this);
}

TDMA::~TDMA()
{
    stop();
}

TDMA::slots_type::size_type TDMA::size(void)
{
    return slots_.size();
}

void TDMA::resize(TDMA::slots_type::size_type n)
{
    slots_.resize(n, false);
    reconfigure();
}

TDMA::slots_type::reference TDMA::operator [](TDMA::slots_type::size_type i)
{
    return slots_.at(i);
}

TDMA::slots_type::iterator TDMA::begin(void)
{
    return slots_.begin();
}

TDMA::slots_type::iterator TDMA::end(void)
{
    return slots_.end();
}

void TDMA::stop(void)
{
    done_ = true;

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();
}

void TDMA::sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt)
{
    Clock::time_point t_next_slot;

    findNextSlot(t, t_next_slot);

    timestampPacket(t_next_slot, std::move(pkt));
}

void TDMA::txWorker(void)
{
    Clock::time_point t_now;            // Current time
    Clock::time_point t_next_slot;      // Time at which our next slot starts
    Clock::time_point t_following_slot; // Time at which our following slot starts

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();

        if (!findNextSlot(t_now, t_next_slot)) {
            // Sleep for 100ms if we don't yet have a slot
            doze(100e-3);
            continue;
        }

        // Schedule transmission for start of our next slot
        txSlot(t_next_slot, tx_slot_samps_);

        // Find following slot
        findNextSlot(t_next_slot + slot_size_, t_following_slot);

        // Sleep until one slot before our following slot
        doze((t_following_slot - t_now - slot_size_).get_real_secs());
    }
}

bool TDMA::findNextSlot(Clock::time_point t, Clock::time_point &t_next)
{
    double t_slot_pos; // Offset into the current slot (sec)
    size_t cur_slot;   // Current slot index
    size_t tx_slot;    // Slots before we can TX

    t_slot_pos = fmod(t, slot_size_);
    cur_slot = fmod(t, frame_size_) / slot_size_;

    for (tx_slot = 1; tx_slot <= slots_.size(); ++tx_slot) {
        if (slots_[(cur_slot + tx_slot) % slots_.size()]) {
            t_next = t + (tx_slot*slot_size_ - t_slot_pos);
            return true;
        }
    }

    return false;
}

void TDMA::reconfigure(void)
{
    frame_size_ = slot_size_*slots_.size();

    SlottedMAC::reconfigure();
}
