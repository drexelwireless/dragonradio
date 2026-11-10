// Host sensing-delay probe (docs/sensing-delay-measurement.rst, step M0).
// Port of src/probe/SenseDelay.hh on codex/ofdma-simulation to the Radio
// interface of this branch.
//
// The probe drives a Radio (USRP or SimUSRP) through the same burstRX and
// burstTX calls the radio uses, one receive chunk (rx_max_samps) per burstRX
// call, and records:
//
//  * per chunk: device time of its last sample and host time when burstRX
//    returned, giving the sample arrival delay D_rx;
//  * device/host clock pairs, taken by a separate thread, so host times can
//    be mapped to device time without a network call in the receive loop;
//  * rising edges of received energy over fixed windows of W samples;
//  * in react mode, a reply burst sent on each detected edge (timed at the
//    device time estimate plus a lead, or untimed);
//  * in ping mode, a short burst sent every period. A node that hears its own
//    ping and a reacting node's reply measures the end-to-end sensing delay
//    on its own clock alone.
#ifndef PROBE_SENSEDELAY_HH_
#define PROBE_SENSEDELAY_HH_

#include <atomic>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Radio.hh"

namespace probe {

enum class Mode { kReceive, kReact, kPing };

struct SenseDelayConfig {
    Mode mode = Mode::kReceive;

    /** @brief Samples per burstRX call; the radio's rx_max_samps (Radio
     * does not expose it, so the caller passes it).
     */
    size_t chunk = 0;

    /** @brief Energy window (samples); edges are resolved to this. */
    size_t window = 32;

    /** @brief Threshold above the noise floor (dB). */
    double threshold_db = 10.0;

    /** @brief Fixed threshold (mean power per sample); 0 calibrates the
     * noise floor over the first noise_secs of the run instead.
     */
    double threshold = 0.0;

    /** @brief Noise-floor calibration time at start (s). */
    double noise_secs = 0.5;

    /** @brief Timed TX lead (s); 0 sends react bursts untimed. */
    double lead = 5e-3;

    /** @brief Ping period (s). */
    double period = 0.05;

    /** @brief React mode: ignore edges for this long after a reply (s), so
     * the node does not react to its own transmission.
     */
    double holdoff = 0.025;

    /** @brief TX burst length (samples) and amplitude. */
    size_t burst_len = 256;
    float  amplitude = 0.5f;

    /** @brief Device/host clock calibration interval (s) and trials. */
    double cal_interval = 1.0;
    unsigned cal_trials = 20;
};

struct SenseDelayResults {
    // Receive chunks
    std::vector<double>   chunk_dev_end;   ///< Device time just after the chunk's last sample
    std::vector<double>   chunk_host;      ///< Host time when burstRX returned
    std::vector<uint32_t> chunk_n;         ///< Samples in the chunk

    // Clock calibration: device = host + offset at host time cal_host
    std::vector<double> cal_host;
    std::vector<double> cal_offset;
    std::vector<double> cal_rtt;

    // Rising edges of received energy
    std::vector<double> edge_dev;          ///< Device time of the window that crossed
    std::vector<double> edge_host;         ///< Host time the edge was seen
    std::vector<float>  edge_power_db;     ///< Window power over the noise floor (dB)

    // React mode
    std::vector<double> react_edge_dev;    ///< Edge that triggered the reply
    std::vector<double> react_tx_dev;      ///< Requested TX time (NaN: untimed)
    std::vector<double> react_host_trigger;///< Host time the reply was triggered
    std::vector<double> react_host_sent;   ///< Host time burstTX returned

    // Ping mode
    std::vector<double> ping_tx_dev;       ///< Requested TX time
    std::vector<double> ping_host_sent;    ///< Host time burstTX returned

    double   noise_power = 0.0;            ///< Mean power per sample, noise floor
    double   threshold = 0.0;              ///< Edge threshold used
    uint64_t tx_late = 0;                  ///< Late timed bursts (device report)
    uint64_t tx_underflow = 0;
    uint64_t rx_errors = 0;                ///< burstRX failures (stream restarted)
    size_t   chunk_samps = 0;              ///< rx_max_samps during the run
    double   rx_rate = 0.0;
};

class SenseDelayProbe {
public:
    SenseDelayProbe(std::shared_ptr<Radio> usrp, const SenseDelayConfig &config);
    ~SenseDelayProbe();

    SenseDelayProbe(const SenseDelayProbe&) = delete;
    SenseDelayProbe& operator=(const SenseDelayProbe&) = delete;

    /** @brief Run for duration seconds, then stop and return the results. */
    SenseDelayResults run(double duration);

    /** @brief Host monotonic time (s), the clock all host times use. */
    static double hostNow(void);

private:
    std::shared_ptr<Radio> usrp_;
    SenseDelayConfig      config_;
    SenseDelayResults     res_;

    std::atomic<bool>   done_;
    std::atomic<double> offset_;        ///< Latest device - host offset (s)
    std::atomic<bool>   offset_valid_;

    // Detector state (receive thread only)
    double  win_acc_ = 0.0;
    size_t  win_fill_ = 0;
    int64_t win_start_tick_ = 0;
    bool    above_ = false;
    std::vector<double> noise_windows_;
    std::atomic<bool> calibrated_;
    double  rx_start_host_ = 0.0;
    double  last_react_host_ = -1e9;

    // React handoff to the TX thread
    std::mutex              tx_mutex_;
    std::condition_variable tx_cond_;
    bool                    tx_pending_ = false;
    double                  tx_edge_dev_ = 0.0;
    double                  tx_trigger_host_ = 0.0;

    std::mutex results_mutex_;  ///< Guards the react/ping/cal vectors

    std::vector<std::complex<float>> burst_;

    void rxLoop(void);
    void txReactLoop(void);
    void txPingLoop(void);
    void calLoop(void);
    void calibrateOnce(void);

    void process(const IQBuf &buf, size_t n, double host);
    void onWindow(int64_t start_tick, double power, double host);

    std::list<std::shared_ptr<IQBuf>> makeBurst(void) const;
    double deviceNowEstimate(void) const;
};

}

#endif /* PROBE_SENSEDELAY_HH_ */
