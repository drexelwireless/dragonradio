// Per-thread work-time accounting (P4 observability).
//
// The channelizer, synthesizer and demodulator workers spin (pause/yield)
// while a streaming IQ buffer fills, so their CPU time in /proc is close to
// 100% whatever the load. A WorkMeter accumulates only the time a worker
// spends processing (inside Scope), so work time / wall time is the thread's
// real utilization. Meters are registered by name in a process-wide registry
// that outlives the threads; a name used again (a worker restarted on
// reconfiguration) continues the same total. Cost: two steady_clock reads
// per scope.
#ifndef UTIL_WORKMETER_HH_
#define UTIL_WORKMETER_HH_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

class WorkMeter {
public:
    using clock = std::chrono::steady_clock;

    explicit WorkMeter(const std::string &name);

    WorkMeter(const std::string &name, unsigned index)
      : WorkMeter(name + std::to_string(index)) {}

    void add(clock::duration d)
    {
        ns_->fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count(),
                       std::memory_order_relaxed);
    }

    /** @brief Times one processing section. */
    class Scope {
    public:
        explicit Scope(WorkMeter &meter) : meter_(meter), start_(clock::now()) {}
        ~Scope() { meter_.add(clock::now() - start_); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        WorkMeter &meter_;
        clock::time_point start_;
    };

    /** @brief Accumulated work time (seconds) of every meter by name. */
    static std::map<std::string, double> snapshot(void);

private:
    std::shared_ptr<std::atomic<int64_t>> ns_;
};

#endif /* UTIL_WORKMETER_HH_ */
