// Simulated radio device for full-stack simulation (phase P2).

#ifndef SIM_SIMUSRP_HH_
#define SIM_SIMUSRP_HH_

#include <complex>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "USRP.hh"

namespace sim {

/** @brief Wire header shared with the air relay (scripts/sim-air.py).
 *
 * All fields are little-endian. A burst's first sample is transmitted at
 * absolute sample index @a start of a grid where sample n occurs at time n/fs
 * on the host's monotonic clock, which every node on the host shares.
 */
struct __attribute__((packed)) AirHeader {
    uint32_t magic;     ///< kAirMagic
    uint16_t version;   ///< kAirVersion
    uint8_t  type;      ///< kAirHello or kAirBurst
    uint8_t  node;      ///< Transmitting node
    int64_t  start;     ///< Absolute sample index of first sample
    double   fs;        ///< Sample rate (Hz)
    double   fc;        ///< Center frequency (Hz)
    uint32_t nsamples;  ///< Number of complex float32 samples that follow
    uint32_t burst;     ///< Per-node message counter
    uint32_t group;     ///< Per-node TX burst (start_of_burst to end_of_burst)
};

static_assert(sizeof(AirHeader) == 44, "AirHeader must be 44 bytes");

constexpr uint32_t kAirMagic = 0x41535244; // "DRSA"
constexpr uint16_t kAirVersion = 2;
constexpr uint8_t  kAirHello = 1;
constexpr uint8_t  kAirBurst = 2;

/** @brief Directed link model, applied at the receiver. */
struct Link {
    /** @brief Power gain applied to the transmitted waveform (dB) */
    double gain_db = 0.0;

    /** @brief Carrier frequency offset (Hz) */
    double cfo_hz = 0.0;

    /** @brief Propagation delay (samples) */
    int64_t delay = 0;

    /** @brief Probability that a transmitted IQ buffer is erased. The
     * time- and frequency-domain synthesizers hand burstTX one buffer per
     * frame; the multichannel synthesizer one per slot.
     */
    double erasure = 0.0;

    /** @brief Extra delay of each TX burst (start_of_burst through
     * end_of_burst), uniform in [0, jitter] samples. Every buffer of a burst
     * is shifted by the same amount.
     */
    int64_t jitter = 0;
};

/** @brief Counters reported by SimUSRP. */
struct Stats {
    uint64_t tx_bursts = 0;          ///< Bursts sent to the air
    uint64_t tx_samples = 0;         ///< Samples sent to the air
    uint64_t tx_late = 0;            ///< Bursts dropped because they were late
    uint64_t tx_underflow = 0;       ///< Continuations that arrived late
    uint64_t rx_bursts = 0;          ///< Bursts received from the air
    uint64_t rx_erased = 0;          ///< Bursts erased by the link model
    uint64_t rx_late = 0;            ///< Bursts that arrived after rendering
    uint64_t rx_partial_late = 0;    ///< Bursts partially rendered before arrival
    uint64_t rx_rate_mismatch = 0;   ///< Bursts at a different sample rate
    uint64_t rx_queue_drops = 0;     ///< Bursts dropped by the bounded queue
    uint64_t rx_samples = 0;         ///< Samples delivered to the MAC
    uint64_t rx_overflows = 0;       ///< Times the reader fell behind
    uint64_t rx_overflow_samples = 0;///< Samples skipped by overflows
    uint64_t rx_streams = 0;         ///< RX stream starts
};

/** @brief A simulated radio device connected to an air relay.
 *
 * Time is the host monotonic clock (MonoClock without a UHD device), so runs
 * are wall-clock paced: RX samples for time t are released at t plus a fixed
 * latency, and a TX burst submitted after its start time is dropped and
 * counted as late, as UHD does. Each receiver renders its own sample stream:
 * complex Gaussian noise plus every other node's bursts after the directed
 * link model (gain, carrier offset, delay, burst erasure) and the frequency
 * difference between the transmitter's and receiver's tuning.
 */
class SimUSRP : public USRP
{
public:
    /** @brief Connect to an air relay.
     * @param air_path Unix socket path of the relay
     * @param node_id This node's ID
     * @param freq Initial TX and RX center frequency (Hz)
     * @param clock_rate Simulated master clock rate (Hz)
     * @param rx_latency Delay between sample time and its release (sec)
     * @param seed Seed for noise and erasures
     */
    SimUSRP(const std::string& air_path,
            uint8_t node_id,
            double freq,
            double clock_rate,
            double rx_latency,
            uint64_t seed);
    virtual ~SimUSRP();

    DeviceType getDeviceType(void) override
    {
        return kSimulated;
    }

    std::vector<std::string> getClockSources(const size_t mboard = 0) override
    {
        return {"internal"};
    }

    std::string getClockSource(const size_t mboard = 0) override
    {
        return "internal";
    }

    void setClockSource(const std::string clock_source,
                        const size_t mboard = kAllMboards) override;

    std::vector<std::string> getTimeSources(const size_t mboard = 0) override
    {
        return {"internal"};
    }

    std::string getTimeSource(const size_t mboard = 0) override
    {
        return "internal";
    }

    void setTimeSource(const std::string &time_source,
                       const size_t mboard = kAllMboards) override;

    double getMasterClockRate(void) override
    {
        return clock_rate_;
    }

    double getTXFrequency(void) override
    {
        return tx_freq_;
    }

    void setTXFrequency(double freq) override
    {
        tx_freq_ = freq;
    }

    double getRXFrequency(void) override
    {
        return rx_freq_;
    }

    void setRXFrequency(double freq) override
    {
        rx_freq_ = freq;
    }

    double getTXRate(void) override
    {
        return tx_rate_;
    }

    void setTXRate(double rate) override
    {
        tx_rate_ = rate;
    }

    double getRXRate(void) override
    {
        return rx_rate_;
    }

    void setRXRate(double rate) override
    {
        rx_rate_ = rate;
    }

    double getTXGain(void) override
    {
        return tx_gain_;
    }

    void setTXGain(float db) override
    {
        tx_gain_ = db;
    }

    double getRXGain(void) override
    {
        return rx_gain_;
    }

    void setRXGain(float db) override
    {
        rx_gain_ = db;
    }

    void setAutoDCOffset(bool enable) override
    {
        auto_dc_offset_ = enable;
    }

    void burstTX(std::optional<MonoClock::time_point> when,
                 bool start_of_burst,
                 bool end_of_burst,
                 std::list<std::shared_ptr<IQBuf>>& bufs) override;

    void stopTXBurst(void) override;

    void startRXStream(MonoClock::time_point when) override;

    void stopRXStream(void) override;

    bool burstRX(MonoClock::time_point when, size_t nsamps, IQBuf& buf) override;

    void setMaxRXSampsFactor(unsigned n) override
    {
        setMaxRXSamps(n*kMaxNumSamps);
    }

    void setMaxTXSampsFactor(unsigned n) override
    {
        setMaxTXSamps(n*kMaxNumSamps);
    }

    void stop(void) override;

    /** @brief Set the directed link model from node @a src to this node. */
    void setLink(uint8_t src, const Link &link);

    /** @brief Get the directed link model from node @a src to this node. */
    Link getLink(uint8_t src);

    /** @brief Receiver noise power per complex sample (dB, unit-power full scale) */
    double getNoisePower(void)
    {
        return noise_db_;
    }

    void setNoisePower(double db);

    /** @brief Get counters. */
    Stats getStats(void);

    /** @brief Samples per packet of the simulated streamer */
    static constexpr size_t kMaxNumSamps = 1024;

    /** @brief Maximum samples queued from the air before bursts are dropped */
    static constexpr size_t kMaxQueuedSamples = 1 << 25;

protected:
    struct Burst {
        uint8_t src;
        int64_t start;  ///< First sample at the receiver (includes link delay)
        double fs;
        double shift_hz;
        float gain;
        std::vector<std::complex<float>> x;

        int64_t end(void) const
        {
            return start + static_cast<int64_t>(x.size());
        }
    };

    const uint8_t node_id_;
    const double clock_rate_;
    const double rx_latency_;

    double tx_freq_;
    double rx_freq_;
    double tx_rate_;
    double rx_rate_;
    double tx_gain_ = 0.0;
    double rx_gain_ = 0.0;

    int fd_;
    std::atomic<bool> done_;

    std::mutex tx_mutex_;
    int64_t tx_pos_;
    bool tx_burst_late_;
    uint32_t tx_burst_count_;
    uint32_t tx_group_;

    std::mutex rx_mutex_;
    std::vector<Burst> bursts_;
    size_t queued_samples_;
    int64_t rx_pos_;
    int64_t rendered_upto_;
    bool streaming_;
    std::map<uint8_t, Link> links_;
    std::map<uint8_t, std::pair<uint32_t, int64_t>> jitter_;  ///< Last TX group and its jitter, per source
    double noise_db_;
    float noise_std_;

    std::mt19937_64 rng_;
    std::mt19937_64 noise_rng_;

    Stats stats_;

    std::thread reader_;

    void reader(void);

    void sendBurst(int64_t start, const std::complex<float> *x, size_t n);

    void render(int64_t n0, size_t n, std::complex<float> *out);

    void waitUntil(const uhd::time_spec_t &t);
};

}

#endif /* SIM_SIMUSRP_HH_ */
