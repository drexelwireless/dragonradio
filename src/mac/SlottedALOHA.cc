#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "USRP.hh"
#include "Util.hh"
#include "mac/SlottedALOHA.hh"

SlottedALOHA::SlottedALOHA(std::shared_ptr<USRP> usrp,
                           std::shared_ptr<PHY> phy,
                           std::shared_ptr<PacketModulator> modulator,
                           std::shared_ptr<PacketDemodulator> demodulator,
                           double bandwidth,
                           double slot_size,
                           double guard_size,
                           double p)
  : SlottedMAC(usrp, phy, modulator, demodulator, bandwidth, slot_size, guard_size)
  , p_(p)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
{
    reconfigure();

    rx_thread_ = std::thread(&SlottedALOHA::rxWorker, this);
    tx_thread_ = std::thread(&SlottedALOHA::txWorker, this);
}

SlottedALOHA::~SlottedALOHA()
{
    stop();
}

void SlottedALOHA::stop(void)
{
    done_ = true;

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();
}

void SlottedALOHA::txWorker(void)
{
    Clock::time_point t_now;       // Current time
    Clock::time_point t_cur_slot;  // Time at which current slot starts
    Clock::time_point t_next_slot; // Time at which our next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();
        t_slot_pos = fmod(t_now.get_real_secs(), slot_size_);
        t_next_slot = t_now + slot_size_ - t_slot_pos;

        // Transmit in this slot with probability p_
        if (dist_(gen_) < p_)
            txSlot(t_next_slot, tx_slot_samps_);

        // Sleep until the next slot
        doze((t_next_slot - t_now).get_real_secs() + slot_size_);
    }
}
