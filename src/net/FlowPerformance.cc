// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "Logger.hh"
#include "net/FlowPerformance.hh"

#define DEBUG 0

#if DEBUG
#define logMgen(...) logEvent(__VA_ARGS__)
#else /* !DEBUG */
#define logMgen(...)
#endif /* !DEBUG */

FlowPerformance::FlowPerformance(double mp)
  : net_in(*this,
           nullptr,
           nullptr,
           std::bind(&FlowPerformance::netPush, this, std::placeholders::_1))
  , net_out(*this,
            nullptr,
            nullptr)
  , radio_in(*this,
             nullptr,
             nullptr,
             std::bind(&FlowPerformance::radioPush, this, std::placeholders::_1))
  , radio_out(*this,
              nullptr,
              nullptr)
  , mp_(mp)
{
}

void FlowPerformance::setMandates(const MandateMap &mandates)
{
    // Set mandates
    {
        std::lock_guard<std::mutex> lock(mandates_mutex_);

        mandates_ = mandates;
    }

    // Update source mandates
    {
        std::lock_guard<std::mutex> lock(sources_mutex_);

        for (auto mandate = mandates.begin(); mandate != mandates.end(); ++mandate) {
            auto it = sources_.find(mandate->first);

            if (it != sources_.end())
                it->second.setMandate(mandate->second);
        }
    }

    // Update sink mandates
    {
        std::lock_guard<std::mutex> lock(sinks_mutex_);

        for (auto mandate = mandates.begin(); mandate != mandates.end(); ++mandate) {
            auto it = sinks_.find(mandate->first);

            if (it != sources_.end())
                it->second.setMandate(mandate->second);
        }
    }
}

void FlowPerformance::netPush(std::shared_ptr<NetPacket> &&pkt)
{
    // Initialize flow info
    pkt->initMGENInfo();

    if (pkt->flow_uid) {
        std::lock_guard<std::mutex> lock(sources_mutex_);
        FlowStats                   &stats = findFlow(sources_, *pkt);

        // Record sent MGEN packet
        const struct mgenhdr *mgenhdr = pkt->getMGENHdr();

        if (mgenhdr) {
            WallClock::time_point ts = mgenhdr->getTimestamp();

            if (start_ && ts > *start_) {
                unsigned mp = WallClock::duration(ts - *start_).count() / mp_;

                pkt->mp = mp;

                stats.record(*pkt, mp);

                logMgen("MGEN: send flow %d seq %d",
                    mgenhdr->getFlowId(),
                    mgenhdr->getSequenceNumber());
            } else
                logMgen("MGEN: send flow %d seq %d (OUT OF MP)",
                    mgenhdr->getFlowId(),
                    mgenhdr->getSequenceNumber());
        }
    }

    net_out.push(std::move(pkt));
}

void FlowPerformance::radioPush(std::shared_ptr<RadioPacket> &&pkt)
{
    const struct ip* iph = pkt->getIPHdr();

    if (iph) {
        // Set packet's flow uid
        switch (iph->ip_p) {
            case IPPROTO_UDP:
            {
                const struct udphdr *udph = pkt->getUDPHdr();

                if (udph)
                    pkt->flow_uid = ntohs(udph->uh_dport);
            }
            break;

            case IPPROTO_TCP:
            {
                const struct tcphdr *tcph = pkt->getTCPHdr();

                if (tcph)
                    pkt->flow_uid = ntohs(tcph->th_dport);
            }
            break;
        }

        // Record flow statistics
        const struct mgenhdr *mgenhdr = pkt->getMGENHdr();

        if (mgenhdr) {
            std::lock_guard<std::mutex> lock(sinks_mutex_);
            FlowStats                   &stats = findFlow(sinks_, *pkt);
            WallClock::time_point       ts = mgenhdr->getTimestamp();
            WallClock::duration         latency = WallClock::now() - ts;

            if (start_ && ts > *start_) {
                unsigned mp = (ts - *start_).count() / mp_;

                if (!stats.mandated_latency || latency <= *stats.mandated_latency) {
                    stats.record(*pkt, mp);

                    logMgen("MGEN: recv flow %d seq %d latency %f",
                        mgenhdr->getFlowId(),
                        mgenhdr->getSequenceNumber(),
                        latency);
                } else
                    logMgen("MGEN: recv flow %d seq %d latency %f (LATE)",
                        mgenhdr->getFlowId(),
                        mgenhdr->getSequenceNumber(),
                        latency);
            } else
                logMgen("MGEN: recv flow %d seq %d latency %f (OUT OF MP)",
                    mgenhdr->getFlowId(),
                    mgenhdr->getSequenceNumber(),
                    latency);
        }
    }

    radio_out.push(std::move(pkt));
}

FlowStatsMap FlowPerformance::getFlowStatsMap(FlowStatsMap &stats,
                                              std::mutex &mutex,
                                              bool reset)
{
    // Get current MP
    unsigned current_mp = (WallClock::now() - *start_).count() / mp_;

    // Get a copy of the statistics
    FlowStatsMap result;

    {
        std::lock_guard<std::mutex> lock(mutex);

        // Expand stats to include current MP
        for (auto it = stats.begin(); it != stats.end(); ++it)
            it->second.stats.resize(current_mp + 1);

        // Get a copy of stats
        result = stats;

        // Reset statistics
        if (reset) {
            for (auto it = stats.begin(); it != stats.end(); ++it)
                it->second.low_mp = it->second.stats.size();
        }
    }

    // Set lowest MP to size of stats if no MP was touched
    for (auto it = result.begin(); it != result.end(); ++it) {
        if (!it->second.low_mp)
            it->second.low_mp = it->second.stats.size();
    }

    return result;
}

FlowStats &FlowPerformance::findFlow(FlowStatsMap &stats, Packet &pkt)
{
    FlowUID   flow_uid = *pkt.flow_uid;
    auto      it = stats.try_emplace(flow_uid, flow_uid, pkt.ehdr().src, pkt.ehdr().dest);
    FlowStats &flow = it.first->second;

    // If we inserted a new flow, add its mandated latency
    if (it.second) {
        std::lock_guard<std::mutex> lock(mandates_mutex_);
        auto                        mandate = mandates_.find(flow_uid);

        if (mandate != mandates_.end())
            flow.setMandate(mandate->second);
    }

    return flow;
}
