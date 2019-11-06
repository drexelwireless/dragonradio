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
  , channelizer_(channelizer)
  , synthesizer_(synthesizer)
  , done_(false)
  , can_transmit_(true)
{
    rx_rate_ = usrp->getRXRate();
    tx_rate_ = usrp->getTXRate();
}

void MAC::reconfigure(void)
{
    rx_rate_ = usrp_->getRXRate();
    tx_rate_ = usrp_->getTXRate();
}
