#include "DummyController.hh"

DummyController::DummyController(std::shared_ptr<Net> net)
  : Controller(net)
{
}

bool DummyController::pull(std::shared_ptr<NetPacket>& pkt)
{
    return net_in.pull(pkt);
}

void DummyController::received(std::shared_ptr<RadioPacket>&& pkt)
{
    if (pkt->data_len != 0 && pkt->nexthop == net_->getMyNodeId())
        radio_out.push(std::move(pkt));
}
