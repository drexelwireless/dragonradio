#include <byteswap.h>

#include "net/FlowSink.hh"

bool FlowSink::process(std::shared_ptr<RadioPacket> &pkt)
{
    FlowProcessor::process(pkt);

    if (!pkt->flow_uid)
        return true;

    std::lock_guard<spinlock_mutex> lock(mutex_);
    auto                            it = flows_.try_emplace(*pkt->flow_uid,
                                             FlowInfo(pkt->src, pkt->dest));
    FlowInfo                        &info = it.first->second;
    Clock::time_point               t_recv = Clock::now();

    // Update latency
    const struct mgenhdr *mgenh = pkt->getMGENHdr();

    if (mgenh) {
        Clock::time_point t_send = mgenh->getTimestamp();
        double            delta = (t_recv - t_send).get_real_secs();

        info.latency.update(t_recv, delta);
        info.min_latency.update(t_recv, delta);
        info.max_latency.update(t_recv, delta);
    }

    // Update throughput and total bytes
    size_t sz = pkt->getPayloadSize();

    info.throughput.update(t_recv, 8*sz);
    info.bytes += sz;

    return true;
}
