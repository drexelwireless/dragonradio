#include "mac/MAC.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         const Channels &rx_channels,
         const Channels &tx_channels,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator)
  : usrp_(usrp)
  , phy_(phy)
  , rx_channels_(rx_channels)
  , tx_channels_(tx_channels)
  , modulator_(modulator)
  , demodulator_(demodulator)
  , tx_channel_(0)
  , timestamped_modulator_(phy->mkModulator())
{
    rx_rate_ = usrp->getRXRate();
    tx_rate_ = usrp->getTXRate();
}

void MAC::reconfigure(void)
{
    rx_rate_ = usrp_->getRXRate();
    tx_rate_ = usrp_->getTXRate();
}

void MAC::timestampPacket(const Clock::time_point &deadline, std::shared_ptr<NetPacket> &&pkt)
{
    std::unique_ptr<ModPacket> mpkt;

    pkt->appendTimestamp(Clock::epoch(), deadline);

    mpkt = std::make_unique<ModPacket>();
    timestamped_modulator_->modulate(pkt, getTXShift(), *mpkt);

    // We modulate the packet before we check to see if we can actually send it
    // becuase we don't want to hold the spinlock for too long. This can result
    // in wasted work, but oh well...
    std::lock_guard<spinlock_mutex> lock(timestamped_mutex_);

    if (timestamped_mpkt_)
        return;

    timestamped_deadline_ = deadline;
    timestamped_mpkt_ = std::move(mpkt);
}
