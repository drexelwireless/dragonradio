#ifndef MANDATEDOUTCOME_HH_
#define MANDATEDOUTCOME_HH_

#include <optional>
#include <unordered_map>

struct MandatedOutcome {
    MandatedOutcome()
      : steady_state_period(1.0)
      , max_drop_rate(0.0)
    {
    }

    MandatedOutcome(double steady_state_period,
                    double max_drop_rate,
                    int point_value,
                    std::optional<double> min_throughput_bps,
                    std::optional<double> max_latency_sec,
                    std::optional<double> deadline)
      : steady_state_period(steady_state_period)
      , max_drop_rate(max_drop_rate)
      , point_value(point_value)
      , min_throughput_bps(min_throughput_bps)
      , max_latency_sec(max_latency_sec)
      , deadline(deadline)
    {
    }

    ~MandatedOutcome() = default;

    /** @brief Period over which to measure outcome metrics (sec) */
    double steady_state_period;

    /** @brief Maximum drop rate as a fraction of traffic */
    double max_drop_rate;

    /** @brief Point value */
    int point_value;

    /** @brief Minimum throughput (bps) */
    std::optional<double> min_throughput_bps;

    /** @brief Maximum latency allowed for a packet (sec) */
    std::optional<double> max_latency_sec;

    /** @brief Delivery deadline (sec) */
    std::optional<double> deadline;
};

using MandatedOutcomeMap = std::unordered_map<FlowUID, MandatedOutcome>;

#endif /* MANDATEDOUTCOME_HH_ */
