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
  , bandwidth_(bandwidth)
{
    rx_rate_ = bandwidth_*phy->getRXRateOversample();
    tx_rate_ = bandwidth_*phy->getTXRateOversample();

    usrp->setRXRate(rx_rate_);
    usrp->setTXRate(tx_rate_);

    phy->setRXRate(rx_rate_);
    phy->setTXRate(tx_rate_);
}
