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
        int64_t secs;
        int32_t usecs;

        if (mgenh->version == DARPA_MGEN_VERSION) {
            const struct darpa_mgenhdr *dmgenh = reinterpret_cast<const struct darpa_mgenhdr*>(mgenh);
            darpa_mgen_secs_t          hdr_secs;
            mgen_usecs_t               hdr_usecs;

            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));

            secs = ntoh_darpa_mgen_secs(hdr_secs);
            usecs = ntohl(hdr_usecs);
        } else {
            mgen_secs_t  hdr_secs;
            mgen_usecs_t hdr_usecs;

            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(mgenh) + offsetof(struct mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(mgenh) + offsetof(struct mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));

            secs = ntoh_mgen_secs(hdr_secs);
            usecs = ntohl(hdr_usecs);
        }

        Clock::time_point t_send = Clock::time_point{static_cast<int64_t>(secs), usecs/1e6};
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
