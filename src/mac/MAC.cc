#include "mac/MAC.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator,
         double bandwidth)
  : usrp_(usrp)
  , phy_(phy)
  , modulator_(modulator)
  , demodulator_(demodulator)
  , timestamped_modulator_(phy->make_modulator())
{
    rx_rate_ = usrp->getRXRate();
    tx_rate_ = usrp->getTXRate();
}

void MAC::timestampPacket(const Clock::time_point &deadline, std::shared_ptr<NetPacket> &&pkt)
{
    std::unique_ptr<ModPacket> mpkt;

    pkt->appendTimestamp(Clock::epoch(), deadline);

    mpkt = std::make_unique<ModPacket>();
    timestamped_modulator_->modulate(*mpkt, pkt);

    // We modulate the packet before we check to see if we can actually send it
    // becuase we don't want to hold the spinlock for too long. This can result
    // in wasted work, but oh well...
    std::lock_guard<spinlock_mutex> lock(timestamped_mutex_);

    if (timestamped_mpkt_)
        return;

    timestamped_deadline_ = deadline;
    timestamped_mpkt_ = std::move(mpkt);
}
