// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "llc/DummyController.hh"

bool DummyController::pull(std::shared_ptr<NetPacket> &pkt)
{
    if (net_in.pull(pkt)) {
        if (!pkt->internal_flags.assigned_seq) {
            Node &nexthop = (*nhood_)[pkt->hdr.nexthop];

            {
                std::lock_guard<std::mutex> lock(seqs_mutex_);
                auto                        it = seqs_.find(nexthop.id);

                if (it != seqs_.end())
                    pkt->hdr.seq = ++(it->second);
                else {
                    pkt->hdr.seq = {0};
                    seqs_.insert(std::make_pair(nexthop.id, Seq{0}));
                }
            }

            pkt->mcsidx = 0;
            pkt->g = nexthop.g;

            pkt->internal_flags.assigned_seq = 1;
        }

        return true;
    } else
        return false;
}

void DummyController::received(std::shared_ptr<RadioPacket> &&pkt)
{
    if (pkt->internal_flags.invalid_header || pkt->internal_flags.invalid_payload)
        return;

    if (pkt->ehdr().data_len != 0 && pkt->hdr.nexthop == nhood_->getThisNodeId())
        radio_out.push(std::move(pkt));
}
