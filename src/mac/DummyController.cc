#include "DummyController.hh"

bool DummyController::pull(std::shared_ptr<NetPacket> &pkt)
{
    if (net_in.pull(pkt)) {
        if (!pkt->internal_flags.has_seq) {
            Node &nexthop = (*net_)[pkt->hdr.nexthop];

            {
                std::lock_guard<spinlock_mutex> lock(seqs_mutex_);
                auto                            it = seqs_.find(nexthop.id);

                if (it != seqs_.end())
                    pkt->hdr.seq = ++(it->second);
                else {
                    pkt->hdr.seq = {0};
                    seqs_.insert(std::make_pair(nexthop.id, Seq{0}));
                }
            }

            pkt->mcsidx = 0;
            pkt->g = nexthop.g;

            pkt->internal_flags.has_seq = 1;
        }

        return true;
    } else
        return false;
}

void DummyController::received(std::shared_ptr<RadioPacket> &&pkt)
{
    if (pkt->internal_flags.invalid_header || pkt->internal_flags.invalid_payload)
        return;

    if (pkt->ehdr().data_len != 0 && pkt->hdr.nexthop == net_->getMyNodeId())
        radio_out.push(std::move(pkt));
}

void DummyController::missed(std::shared_ptr<NetPacket> &&pkt)
{
}

void DummyController::transmitted(Synthesizer::Slot &slot)
{
}
