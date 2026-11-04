// Simulated radio device for full-stack simulation (phase P2).
//
// Ported from codex/ofdma-simulation, where SimUSRP derived from that
// branch's concrete USRP class and the simulated node clock was a global
// (Clock::setSimulatedClock). Here it implements the abstract Radio device
// interface and is itself the node's clock (MonoClock::TimeKeeper), so the
// simulated clock offset and rate error belong to the device.

#ifndef SIM_SIMUSRP_HH_
#define SIM_SIMUSRP_HH_

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Clock.hh"
#include "Radio.hh"

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
    double   clock_ppm; ///< Transmitter oscillator error (ppm): sample clock and carrier (v3)
};

static_assert(sizeof(AirHeader) == 52, "AirHeader must be 52 bytes");

constexpr uint32_t kAirMagic = 0x41535244; // "DRSA"
// v3: start is the first sample's index on the shared host clock (true
// time), not the transmitter's own device clock, and clock_ppm was added, so
// nodes with independent clocks interoperate. Same wire format as on
// codex/ofdma-simulation.
constexpr uint16_t kAirVersion = 3;
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
     * channel synthesizer hands burstTX one buffer per frame (or per
     * OFDMA burst); the multichannel synthesizer one per slot.
     */
    double erasure = 0.0;

    /** @brief Extra delay of each TX burst (start_of_burst through
     * end_of_burst), uniform in [0, jitter] samples. Every buffer of a burst
     * is shifted by the same amount.
     */
    int64_t jitter = 0;

    /** @brief Transmitter sample clock offset relative to this receiver
     * (parts per million). Positive: the transmitter's clock is fast, so a
     * burst of N samples spans N/(1 + 1e-6 sfo_ppm) receiver samples. Bursts
     * are resampled with a windowed-sinc interpolator.
     */
    double sfo_ppm = 0.0;
};

/** @brief Hardware-like receiver and gain impairments of one device.
 *
 * Everything is off by default. At the receiver, the rendered sum of signal
 * and thermal noise is scaled by the RX gain, passed through IQ imbalance,
 * offset by DC and clipped at the ADC full scale, in that order. With the
 * gain model on, the transmitter scales each burst by its TX gain and the
 * receiver scales signal and noise by its RX gain, both relative to
 * reference gains, so link gain_db is the path gain and noise_power is
 * referred to the receiver input at the reference RX gain.
 */
struct Impairments {
    bool   gain_model = false;      ///< Apply TX and RX gain
    double tx_gain_ref_db = 0.0;    ///< TX gain giving unit scaling
    double rx_gain_ref_db = 0.0;    ///< RX gain giving unit scaling
    double adc_full_scale = 0.0;    ///< Clip I and Q at +/- this (0: no clipping)
    double iq_amp_db = 0.0;         ///< Q-branch gain relative to I (dB)
    double iq_phase_deg = 0.0;      ///< Q-branch phase skew (degrees)
    std::complex<float> dc{0.0f, 0.0f}; ///< DC offset added after IQ imbalance
    /** @brief Couple this device's own transmissions into its receiver at
     * this gain (dB), as TX-to-RX leakage does on a real front end; unset (the
     * default) leaves the receiver deaf to its own transmissions.
     */
    /** The old branch used NaN for "unset". Under -Ofast (-ffinite-math-only)
     * std::isnan is folded to false, which turned leakage on with a NaN gain
     * and put NaN into every receiver while its node transmitted.
     */
    std::optional<double> tx_leakage_db;

    bool receiverActive(void) const
    {
        return gain_model || adc_full_scale > 0 || iq_amp_db != 0 || iq_phase_deg != 0
            || dc != std::complex<float>(0.0f, 0.0f);
    }
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
    uint64_t rx_clipped = 0;         ///< I or Q components clipped by the ADC model
    uint64_t tx_nonfinite = 0;       ///< Non-finite samples handed to burstTX (sent as is)
    uint64_t rx_nonfinite = 0;       ///< Non-finite samples delivered by burstRX
};

/** @brief A simulated radio device connected to an air relay.
 *
 * Time is the host monotonic clock (steady_clock), optionally with this
 * node's own offset and rate error (setClock), so runs are wall-clock paced:
 * RX samples for time t are released at t plus a fixed latency, and a TX
 * burst submitted after its start time is dropped and counted as late, as
 * UHD does. Each receiver renders its own sample stream: complex Gaussian
 * noise plus every other node's bursts after the directed link model (gain,
 * carrier offset, delay, burst erasure, jitter, sample clock offset) and the
 * frequency difference between the transmitter's and receiver's tuning.
 *
 * It never opens or falls back to a UHD device: if the relay cannot be
 * reached, construction fails.
 */
class SimUSRP : public Radio
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

    SimUSRP() = delete;
    SimUSRP(const SimUSRP&) = delete;
    SimUSRP& operator=(const SimUSRP&) = delete;

    double getMasterClockRate(void) const override
    {
        return clock_rate_;
    }

    double getTXFrequency(void) const override
    {
        return tx_freq_.load(std::memory_order_relaxed);
    }

    void setTXFrequency(double freq) override
    {
        tx_freq_.store(freq, std::memory_order_relaxed);
    }

    double getRXFrequency(void) const override
    {
        return rx_freq_.load(std::memory_order_relaxed);
    }

    void setRXFrequency(double freq) override
    {
        rx_freq_.store(freq, std::memory_order_relaxed);
    }

    double getTXRate(void) const override
    {
        return tx_rate_.load(std::memory_order_relaxed);
    }

    void setTXRate(double rate) override
    {
        tx_rate_.store(rate, std::memory_order_relaxed);
    }

    double getRXRate(void) const override
    {
        return rx_rate_.load(std::memory_order_relaxed);
    }

    void setRXRate(double rate) override
    {
        rx_rate_.store(rate, std::memory_order_relaxed);
    }

    double getTXGain(void) const override
    {
        return tx_gain_.load(std::memory_order_relaxed);
    }

    void setTXGain(float db) override
    {
        tx_gain_.store(db, std::memory_order_relaxed);
    }

    double getRXGain(void) const override
    {
        return rx_gain_.load(std::memory_order_relaxed);
    }

    void setRXGain(float db) override
    {
        rx_gain_.store(db, std::memory_order_relaxed);
    }

    bool inTXBurst() const override
    {
        return in_tx_burst_.load(std::memory_order_acquire);
    }

    std::optional<MonoClock::time_point> getNextTXTime() const override;

    void zeroStuff(ssize_t n) override;

    void burstTX(std::optional<MonoClock::time_point> when,
                 bool start_of_burst,
                 bool end_of_burst,
                 std::list<std::shared_ptr<IQBuf>>& bufs) override;

    void stopTXBurst(void) override;

    void startRXStream(std::optional<MonoClock::time_point> when = std::nullopt) override;

    void stopRXStream(void) override;

    bool burstRX(std::optional<MonoClock::time_point> when, size_t nsamps, IQBuf& buf) override;

    size_t getRecommendedBurstRXSize(size_t nsamps) const override
    {
        return nsamps + 8*getMaxRXSamps();
    }

    uint64_t getTXUnderflowCount(void) override
    {
        return tx_underflow_count_.exchange(0, std::memory_order_relaxed);
    }

    uint64_t getTXLateCount(void) override
    {
        return tx_late_count_.exchange(0, std::memory_order_relaxed);
    }

    void stop(void) override;

    /** @brief The device clock: host monotonic time, with this node's
     * simulated offset and rate error (setClock).
     */
    MonoClock::time_point now() noexcept override;

    /** @brief Give the device clock an offset and rate error.
     * @param offset Device time minus host time now (seconds)
     * @param ppm Device clock rate error (parts per million)
     */
    /** Device time is host + offset + 1e-6 ppm (host - epoch), where epoch is
     * the host time of this call. The same oscillator error sets the sample
     * clock and carrier of this node's transmissions as other nodes receive
     * them, and of its own receiver. Defaults (0, 0) keep the host clock.
     * Set it before the device becomes the time keeper.
     */
    void setClock(double offset, double ppm);

    /** @brief Simulated device clock offset (seconds). */
    double getClockOffset(void) const
    {
        return clk_offset_.load(std::memory_order_relaxed);
    }

    /** @brief Simulated device clock rate error (ppm). */
    double getClockPPM(void) const
    {
        return 1e6*(clk_rate_.load(std::memory_order_acquire) - 1.0);
    }

    /** @brief Host monotonic time (sec) corresponding to a device time (sec). */
    double deviceToHost(double device) const;

    /** @brief Device time (sec) corresponding to a host monotonic time (sec). */
    double hostToDevice(double host) const;

    /** @brief Get automatic DC offset correction (accepted and ignored). */
    bool getAutoDCOffset() const
    {
        return auto_dc_offset_;
    }

    /** @brief Set automatic DC offset correction (accepted and ignored). */
    void setAutoDCOffset(bool enable)
    {
        auto_dc_offset_ = enable;
    }

    /** @brief Maximum number of samples per burstRX chunk. */
    size_t getMaxRXSamps(void) const
    {
        return rx_max_samps_.load(std::memory_order_relaxed);
    }

    void setMaxRXSamps(size_t count)
    {
        rx_max_samps_.store(count, std::memory_order_relaxed);
    }

    void setMaxRXSampsFactor(unsigned n)
    {
        setMaxRXSamps(n*kMaxNumSamps);
    }

    /** @brief Maximum number of samples per TX packet (bookkeeping only). */
    size_t getMaxTXSamps(void) const
    {
        return tx_max_samps_.load(std::memory_order_relaxed);
    }

    void setMaxTXSamps(size_t count)
    {
        tx_max_samps_.store(count, std::memory_order_relaxed);
    }

    void setMaxTXSampsFactor(unsigned n)
    {
        setMaxTXSamps(n*kMaxNumSamps);
    }

    /** @brief Set the directed link model from node @a src to this node. */
    void setLink(uint8_t src, const Link &link);

    /** @brief Get the directed link model from node @a src to this node. */
    Link getLink(uint8_t src);

    /** @brief Set hardware-like impairments (see Impairments). */
    void setImpairments(const Impairments &imp);

    /** @brief Get hardware-like impairments. */
    Impairments getImpairments(void);

    /** @brief Receiver noise power per complex sample (dB, unit-power full scale) */
    double getNoisePower(void);

    void setNoisePower(double db);

    /** @brief Get counters. */
    Stats getStats(void);

    /** @brief Node ID of this device. */
    uint8_t getNodeId(void) const
    {
        return node_id_;
    }

    /** @brief Samples per packet of the simulated streamer */
    static constexpr size_t kMaxNumSamps = 1024;

    /** @brief Maximum samples queued from the air before bursts are dropped */
    static constexpr size_t kMaxQueuedSamples = 1 << 25;

    /** @brief Sample index of time @a t at rate @a fs (rounded, as UHD's to_ticks). */
    static int64_t toTicks(const MonoClock::time_point &t, double fs);

    /** @brief First sample index at or after time @a t. */
    static int64_t ceilTicks(const MonoClock::time_point &t, double fs);

    /** @brief Time of sample index @a n at rate @a fs. */
    static MonoClock::time_point fromTicks(int64_t n, double fs);

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

    std::atomic<double> tx_freq_;
    std::atomic<double> rx_freq_;
    std::atomic<double> tx_rate_;
    std::atomic<double> rx_rate_;
    std::atomic<double> tx_gain_;
    std::atomic<double> rx_gain_;
    std::atomic<size_t> tx_max_samps_;
    std::atomic<size_t> rx_max_samps_;
    bool auto_dc_offset_ = false;

    /** @brief Simulated clock: offset (sec), rate (1 + 1e-6 ppm), epoch (host sec). */
    std::atomic<double> clk_offset_;
    std::atomic<double> clk_rate_;
    std::atomic<double> clk_epoch_;

    int fd_;
    std::atomic<bool> done_;

    std::atomic<bool> in_tx_burst_;
    std::atomic<uint64_t> tx_underflow_count_;
    std::atomic<uint64_t> tx_late_count_;

    mutable std::mutex tx_mutex_;
    std::optional<MonoClock::time_point> t_next_tx_;
    int64_t tx_pos_;
    bool tx_burst_late_;
    uint32_t tx_burst_count_;
    uint32_t tx_group_;
    std::vector<std::complex<float>> tx_scaled_;

    std::mutex rx_mutex_;
    std::vector<Burst> bursts_;
    size_t queued_samples_;
    int64_t rx_pos_;
    int64_t rendered_upto_;
    bool streaming_;
    std::map<uint8_t, Link> links_;
    Impairments impairments_;
    std::map<uint8_t, std::pair<uint32_t, int64_t>> jitter_;  ///< Last TX group and its jitter, per source
    double noise_db_;
    float noise_std_;

    std::mt19937_64 rng_;

    /** @brief Complex Gaussian noise: xoshiro256** and Box-Muller (exact
     * Gaussian up to float precision; about 5x cheaper than
     * std::normal_distribution).
     */
    struct NoiseSource {
        uint64_t s[4];

        explicit NoiseSource(uint64_t seed)
        {
            // splitmix64 seeding
            for (auto &w : s) {
                uint64_t z = (seed += 0x9e3779b97f4a7c15ull);
                z = (z ^ (z >> 30))*0xbf58476d1ce4e5b9ull;
                z = (z ^ (z >> 27))*0x94d049bb133111ebull;
                w = z ^ (z >> 31);
            }
        }

        uint64_t next(void)
        {
            const uint64_t r = rotl(s[1]*5, 7)*9, t = s[1] << 17;
            s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotl(s[3], 45);
            return r;
        }

        /** @brief One complex sample with independent N(0, sigma^2) parts. */
        std::complex<float> operator()(float sigma)
        {
            const uint64_t x = next();
            // u1 in (0, 1] (never 0, so the log is finite), u2 in [0, 1).
            const float u1 = (static_cast<float>(x >> 40) + 1.0f)*0x1.0p-24f;
            const float u2 = static_cast<float>((x >> 16) & 0xffffff)*0x1.0p-24f;
            const float r = sigma*std::sqrt(-2.0f*std::log(u1));
            float       sn, cs;

            sincosf(6.2831853071795864769f*u2, &sn, &cs);
            return std::complex<float>(r*cs, r*sn);
        }

        static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    };

    NoiseSource noise_rng_;

    Stats stats_;

    std::thread reader_;

    void reader(void);

    /** @brief Advance the TX position by @a n samples (tx_mutex_ held). */
    void advanceTX(ssize_t n);

    void sendBurst(int64_t start, const std::complex<float> *x, size_t n);

    void render(int64_t n0, size_t n, std::complex<float> *out);

    /** @brief RX gain, IQ imbalance, DC offset and ADC clipping (rx_mutex_ held). */
    void applyReceiverImpairments(std::complex<float> *y, size_t n);

    /** @brief Sleep until device time @a t (or stop). */
    void waitUntil(const MonoClock::time_point &t);
};

}

#endif /* SIM_SIMUSRP_HH_ */
