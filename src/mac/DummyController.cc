#include "DummyController.hh"

DummyController::DummyController(std::shared_ptr<Net> net,
                                 const std::vector<TXParams> &tx_params)
  : Controller(net, tx_params)
{
}

bool DummyController::pull(std::shared_ptr<NetPacket>& pkt)
{
    if (net_in.pull(pkt)) {
        if (!pkt->isInternalFlagSet(kHasSeq)) {
            Node &nexthop = (*net_)[pkt->nexthop];

            pkt->seq = 0;
            pkt->tx_params = &tx_params_[0];
            pkt->g = tx_params_[0].g_0dBFS.getValue() * nexthop.g;

            pkt->setInternalFlag(kHasSeq);
        }

        return true;
    } else
        return false;
}

void DummyController::received(std::shared_ptr<RadioPacket>&& pkt)
{
    if (pkt->isInternalFlagSet(kInvalidHeader) || pkt->isInternalFlagSet(kInvalidPayload))
        return;

    if (pkt->data_len != 0 && pkt->nexthop == net_->getMyNodeId())
        radio_out.push(std::move(pkt));
}

void DummyController::missed(std::shared_ptr<NetPacket>&& pkt)
{
}

void DummyController::transmitted(std::shared_ptr<NetPacket>& pkt)
{
}
