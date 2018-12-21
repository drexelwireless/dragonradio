#ifndef FLOWINFO_HH_
#define FLOWINFO_HH_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <mutex>
#include <unordered_map>

#include "spinlock_mutex.hh"
#include "Clock.hh"
#include "Packet.hh"
#include "net/MandatedOutcome.hh"
#include "net/Processor.hh"
#include "stats/TimeWindowEstimator.hh"

struct FlowInfo {
    FlowInfo(NodeId src, NodeId dest)
      : src(src)
      , dest(dest)
      , bytes(0)
    {
    }

    FlowInfo() = delete;

    ~FlowInfo() = default;

    /** @brief Flow source */
    NodeId src;

    /** @brief Flow destination */
    NodeId dest;

    /** @brief Mean latency (sec) */
    TimeWindowMean<Clock, double> latency;

    /** @brief Minimum latency (sec) */
    TimeWindowMin<Clock, double> min_latency;

    /** @brief Maximum latency (sec) */
    TimeWindowMax<Clock, double> max_latency;

    /** @brief Mean throughput (bps) */
    TimeWindowMeanRate<Clock, double> throughput;

    /** @brief Bytes */
    uint64_t bytes;
};

using FlowInfoMap = std::unordered_map<FlowUID, FlowInfo>;

/** @brief A flow processor. */
template <class T>
class FlowProcessor : public Processor<T>
{
public:
    explicit FlowProcessor(double mp)
      : mp_(mp)
    {
    }

    FlowProcessor() = delete;

    virtual ~FlowProcessor() = default;

    /** @brief Get measurement period */
    double getMeasurementPeriod(void)
    {
        return mp_;
    }

    /** @brief Set measurement period */
    void setMeasurementPeriod(double mp)
    {
        mp_ = mp;

        std::lock_guard<spinlock_mutex> lock(mutex_);

        for (auto it : flows_) {
            it.second.latency.setTimeWindow(mp);
            it.second.min_latency.setTimeWindow(mp);
            it.second.max_latency.setTimeWindow(mp);
            it.second.throughput.setTimeWindow(mp);
        }
    }

    /** @brief Return flow info */
    FlowInfoMap getFlowInfo(void)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        return flows_;
    }

    /** @brief Get mandates */
    MandatedOutcomeMap getMandates(void)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        return mandates_;
    }

    /** @brief Set mandates */
    void setMandates(const MandatedOutcomeMap &mandates)
    {
        std::lock_guard<spinlock_mutex> lock(mutex_);

        mandates_ = mandates;
        flows_.clear();
    }

protected:
    /** @brief Measurement period */
    double mp_;

    /** @brief Mutex for flow info and mandates */
    spinlock_mutex mutex_;

    /** @brief Flow info */
    FlowInfoMap flows_;

    /** @brief Mandates */
    MandatedOutcomeMap mandates_;

    /** @brief Tag a packet */
    virtual bool process(T& pkt) override
    {
        struct ip *iph;
        uint8_t   ip_p;

        iph = pkt->getIPHdr(&ip_p);
        if (!iph)
            return true;

        switch (ip_p) {
            case IPPROTO_UDP:
            {
                struct udphdr *udph = pkt->getUDPHdr();

                if (udph)
                    pkt->flow_uid = ntohs(udph->uh_dport);
            }
            break;

            case IPPROTO_TCP:
            {
                struct tcphdr *tcph = pkt->getTCPHdr();

                if (tcph)
                    pkt->flow_uid = ntohs(tcph->th_dport);
            }
            break;
        }

        return true;
    }
};

#endif /* FLOWINFO_HH_ */
