#include "mac/MAC.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<Synthesizer> synthesizer)
  : usrp_(usrp)
  , phy_(phy)
  , controller_(controller)
  , snapshot_collector_(collector)
  , can_transmit_(true)
  , channelizer_(channelizer)
  , synthesizer_(synthesizer)
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

    pkt->appendTimestamp(Clock::to_mono_time(deadline));

    mpkt = std::make_unique<ModPacket>();
    synthesizer_->modulateOne(pkt, *mpkt);

    // We modulate the packet before we check to see if we can actually send it
    // becuase we don't want to hold the spinlock for too long. This can result
    // in wasted work, but oh well...
    std::lock_guard<spinlock_mutex> lock(timestamped_mutex_);

    if (timestamped_mpkt_)
        return;

    timestamped_deadline_ = deadline;
    timestamped_mpkt_ = std::move(mpkt);
}
