#include "net/FlowSource.hh"

bool FlowSource::process(std::shared_ptr<NetPacket> &pkt)
{
    FlowProcessor::process(pkt);

    if (!pkt->flow_uid)
        return true;

    std::lock_guard<spinlock_mutex> lock(mutex_);
    FlowUID                         flow = *pkt->flow_uid;
    auto                            it = flows_.try_emplace(flow,
                                             FlowInfo(pkt->src, pkt->dest));
    FlowInfo                        &info = it.first->second;
    Clock::time_point               t_send = Clock::now();

    // Update throughput and total bytes
    size_t sz = pkt->getPayloadSize();

    info.throughput.update(t_send, 8*sz);
    info.bytes += sz;

    // Tag packet with deadline if it's associated mandate has one
    auto mandate_it = mandates_.find(flow);

    if (mandate_it != mandates_.end()) {
        MandatedOutcome &mandate = mandate_it->second;

        if (mandate.max_latency_sec)
            pkt->deadline = pkt->timestamp + *mandate.max_latency_sec;
        else if (mandate.deadline)
            pkt->deadline = pkt->timestamp + *mandate.deadline;
    }

    return true;
}
