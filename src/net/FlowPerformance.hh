// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FLOWPERFORMANCE_HH_
#define FLOWPERFORMANCE_HH_

#include <mutex>
#include <optional>
#include <unordered_map>

#include "Clock.hh"
#include "Packet.hh"
#include "cil/CIL.hh"
#include "net/Processor.hh"
#include "stats/TimeWindowEstimator.hh"

/** @brief Statistics for a single measurement period */
struct MPStats {
    MPStats() : npackets(0), nbytes(0) {}

    /** @brief Number of packets sent/received */
    size_t npackets;

    /** @brief Number of bytes sent/received */
    size_t nbytes;
};

/** @brief Statistics for a single flow */
struct FlowStats {
    FlowStats(FlowUID flow_uid, NodeId src, NodeId dest)
      : flow_uid(flow_uid)
      , src(src)
      , dest(dest)
    {
        // Reserve enough room for 30min worth of entries by default (assuming a
        // measurement period of 1 sec)
        stats.reserve(30*60);
    }

    FlowStats() = delete;

    /** @brief Flow UID */
    FlowUID flow_uid;

    /** @brief Flow source */
    NodeId src;

    /** @brief Flow destination */
    NodeId dest;

    /** @brief Flow mandated latency */
    std::optional<double> mandated_latency;

    /** @brief Lowest MP modified */
    std::optional<size_t> low_mp;

    /** @brief Flow statistics for each measurement period */
    std::vector<MPStats> stats;

    /** @brief Record statistics for a packet */
    void record(Packet &pkt, size_t mp)
    {
        if (mp >= stats.size())
            stats.resize(mp + 1);

        if (!low_mp || mp < low_mp)
            low_mp = mp;

        ++stats[mp].npackets;
        stats[mp].nbytes += pkt.payload_size;
    }

    /** @brief Set a flow's mandates */
    void setMandate(const Mandate &mandate)
    {
        mandated_latency = mandate.mandated_latency;
    }
};

using FlowStatsMap = std::unordered_map<FlowUID, FlowStats>;

/** @brief A flow performance measurement element. */
class FlowPerformance : public Element
{
public:
    explicit FlowPerformance(double mp);

    FlowPerformance() = delete;

    virtual ~FlowPerformance() = default;

    /** @brief Get measurement period */
    double getMeasurementPeriod(void) const
    {
        return mp_;
    }

    /** @brief Get start time */
    std::optional<double> getStart(void) const
    {
        return start_;
    }

    /** @brief Set start time */
    void setStart(std::optional<double> &start)
    {
        start_ = start;
    }

    /** @brief Return flow source statistics */
    FlowStatsMap getSources(bool reset)
    {
        return getFlowStatsMap(sources_, sources_mutex_, reset);
    }

    /** @brief Return flow sink statistics */
    FlowStatsMap getSinks(bool reset)
    {
        return getFlowStatsMap(sinks_, sinks_mutex_, reset);
    }

    /** @brief Get mandates */
    MandateMap getMandates(void)
    {
        std::lock_guard<std::mutex> lock(mandates_mutex_);

        return mandates_;
    }

    /** @brief Set mandates */
    void setMandates(const MandateMap &mandates);

    /** @brief Network packet input port. */
    NetIn<Push> net_in;

    /** @brief Network packet output port. */
    NetOut<Push> net_out;

    /** @brief Radio packet input port. */
    RadioIn<Push> radio_in;

    /** @brief Radio packet output port. */
    RadioOut<Push> radio_out;

protected:
    /** @brief Measurement period */
    double mp_;

    /** @brief Start time */
    std::optional<double> start_;

    /** @brief Mutex for sources */
    std::mutex sources_mutex_;

    /** @brief Flow source info */
    FlowStatsMap sources_;

    /** @brief Mutex for sinks */
    std::mutex sinks_mutex_;

    /** @brief Flow sink info */
    FlowStatsMap sinks_;

    /** @brief Mandates mutex */
    std::mutex mandates_mutex_;

    /** @brief Mandates */
    MandateMap mandates_;

    /** @brief Handle a network packet */
    void netPush(std::shared_ptr<NetPacket> &&pkt);

    /** @brief Handle a radio packet */
    void radioPush(std::shared_ptr<RadioPacket> &&pkt);

    /** @brief Return a copy of a FlowStatsMap, resetting it if necessary */
    FlowStatsMap getFlowStatsMap(FlowStatsMap &stats,
                                 std::mutex &mutex,
                                 bool reset);

    /** @brief Find a flow's entry in statistics map */
    FlowStats &findFlow(FlowStatsMap &stats, Packet &pkt);
};

#endif /* FLOWPERFORMANCE_HH_ */
