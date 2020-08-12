#ifndef CIL_HH_
#define CIL_HH_

#include <optional>
#include <unordered_map>

#include "net/Net.hh"

struct Mandate {
    Mandate() = delete;

    Mandate(FlowUID flow_uid_,
            double hold_period_,
            int point_value_,
            std::optional<double> max_latency_s_,
            std::optional<double> min_throughput_bps_,
            std::optional<double> file_transfer_deadline_s_)
      : flow_uid(flow_uid_)
      , hold_period(hold_period_)
      , point_value(point_value_)
      , max_latency_s(max_latency_s_)
      , min_throughput_bps(min_throughput_bps_)
      , file_transfer_deadline_s(file_transfer_deadline_s_)
      , achieved_duration(0)
      , scalar_performace(0.0)
    {
        if (max_latency_s)
            mandated_latency = *max_latency_s;
        else if (file_transfer_deadline_s)
            mandated_latency = *file_transfer_deadline_s;
    }

    ~Mandate() = default;

    /** @brief Is this a throughput mandate? */
    bool isThroughput() const
    {
        return min_throughput_bps.has_value();
    }

    /** @brief Is this a file transfer mandate? */
    bool isFileTransfer() const
    {
        return file_transfer_deadline_s.has_value();
    }

    /** @brief Flow UID */
    FlowUID flow_uid;

    /** @brief Period over which to measure outcome metrics (sec) */
    double hold_period;

    /** @brief Point value */
    int point_value;

    /** @brief Maximum latency allowed for a packet (sec) */
    std::optional<double> max_latency_s;

    /** @brief Minimum throughput (bps) */
    std::optional<double> min_throughput_bps;

    /** @brief File transfer delivery deadline (sec) */
    std::optional<double> file_transfer_deadline_s;

    /** @brief Achieved duration */
    unsigned achieved_duration;

    /** @brief Scalar performance */
    double scalar_performace;

    /** @brief Flow mandated latency */
    std::optional<double> mandated_latency;

    /** @brief Nodes in flow */
    std::vector<NodeId> radio_ids;
};

using MandateMap = std::unordered_map<FlowUID, Mandate>;

#endif /* CIL_HH_ */
