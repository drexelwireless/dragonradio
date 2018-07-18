#include "DummyController.hh"

DummyController::DummyController(std::shared_ptr<Net> net)
  : Controller(net)
{
}

bool DummyController::pull(std::shared_ptr<NetPacket>& pkt)
{
    if (net_in.pull(pkt)) {
        if (!pkt->isInternalFlagSet(kHasSeq)) {
            Node &nexthop = (*net_)[pkt->nexthop];

            pkt->seq = nexthop.seq++;
            pkt->tx_params = nexthop.tx_params;
            pkt->g = nexthop.tx_params->g_0dBFS.getValue() * nexthop.g;

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
