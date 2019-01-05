#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "USRP.hh"
#include "Util.hh"
#include "mac/SlottedALOHA.hh"

SlottedALOHA::SlottedALOHA(std::shared_ptr<USRP> usrp,
                           std::shared_ptr<PHY> phy,
                           std::shared_ptr<Controller> controller,
                           std::shared_ptr<SnapshotCollector> collector,
                           const Channels &rx_channels,
                           const Channels &tx_channels,
                           std::shared_ptr<PacketModulator> modulator,
                           std::shared_ptr<PacketDemodulator> demodulator,
                           double slot_size,
                           double guard_size,
                           double p)
  : SlottedMAC(usrp,
               phy,
               controller,
               collector,
               rx_channels,
               tx_channels,
               modulator,
               demodulator,
               slot_size,
               guard_size)
  , p_(p)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
  , arrival_dist_(p)
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

void SlottedALOHA::sendTimestampedPacket(const Clock::time_point &t, std::shared_ptr<NetPacket> &&pkt)
{
    size_t tx_slot;

    tx_slot = t.get_real_secs() / slot_size_ + arrival_dist_(gen_);

    timestampPacket(Clock::time_point { tx_slot * slot_size_ }, std::move(pkt));
}

void SlottedALOHA::txWorker(void)
{
    Clock::time_point t_now;       // Current time
    Clock::time_point t_next_slot; // Time at which our next slot starts
    double            t_slot_pos;  // Offset into the current slot (sec)

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();
        t_slot_pos = fmod(t_now, slot_size_);
        t_next_slot = t_now + (slot_size_ - t_slot_pos);

        // Transmit in the next slot with probability p_...
        bool transmit = dist_(gen_) < p_;

        // ...or if we have a timestamped packet to send.
        {
            std::lock_guard<spinlock_mutex> lock(timestamped_mutex_);

            if (timestamped_mpkt_ && approx(timestamped_deadline_, t_next_slot))
                transmit = true;
        }

        if (transmit) {
            txSlot(t_next_slot, tx_slot_samps_, false);

            // Modulate samples for next slot
            modulator_->modulate(premod_samps_);
        }

        // Sleep until the next slot
        doze((t_next_slot - t_now).get_real_secs());
    }
}
