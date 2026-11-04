// Synthetic IP traffic in place of the tun/tap device (phase P2).

#ifndef SIM_SIMTRAFFIC_HH_
#define SIM_SIMTRAFFIC_HH_

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Logger.hh"
#include "Packet.hh"
#include "net/Element.hh"

namespace sim {

/** @brief Generates and receives Ethernet/IPv4/UDP frames exactly as a tap
 * interface would hand them to the radio, without creating one.
 *
 * Each frame's UDP payload carries an application header: magic, source,
 * destination, flow port, sequence number, transmit time on the shared
 * monotonic clock, length and an FNV-1a hash over the payload, followed by
 * pseudo-random filler derived from the sequence number. The receiver checks
 * the hash, removes duplicates and records one-way latency per flow.
 */
class SimTraffic : public Element
{
public:
    struct Flow {
        uint16_t port;
        NodeId   dest;
        double   rate;      ///< Packets per second
        size_t   size;      ///< UDP payload bytes
        uint64_t count;     ///< Packets to send (0 = until stopped)
        double   start;     ///< Delay after start() (sec)
        uint64_t sent = 0;
        uint64_t send_failures = 0;
    };

    struct RecvStats {
        uint64_t unique = 0;
        uint64_t duplicates = 0;
        uint64_t corrupt = 0;
        uint64_t out_of_order = 0;
        uint64_t max_seq = 0;
        uint64_t bytes = 0;
        double first_rx = 0;         ///< First unique arrival (MonoClock sec)
        double last_rx = 0;          ///< Last unique arrival (MonoClock sec)
        std::vector<double> latency; ///< One-way latency of unique packets (sec)
        std::vector<uint64_t> seqs;  ///< Unique sequence numbers in arrival order
        std::vector<double> arrivals; ///< Arrival time of each unique packet (MonoClock sec)
        std::vector<uint64_t> dup_seqs; ///< Sequence numbers delivered more than once
        // Compact mode (setDetail(false)) keeps bounded state instead of the
        // per-packet vectors above, for long soak runs (P4):
        double latency_sum = 0;       ///< Sum of one-way latencies (sec)
        double latency_min = 0;       ///< Minimum one-way latency (sec)
        double latency_max = 0;       ///< Maximum one-way latency (sec)
        std::vector<uint64_t> latency_hist; ///< Counts per kLatencyBins bin
        std::vector<uint32_t> per_second; ///< Unique arrivals per second since first_rx
        uint64_t stale = 0;           ///< Arrivals older than the duplicate window (not counted unique)
        std::vector<uint64_t> window; ///< Seen bitmap of the last kWindow sequence numbers
    };

    /** @brief Latency histogram: bin i covers [kLatencyMin * kLatencyRatio^i,
     * kLatencyMin * kLatencyRatio^(i+1)); bin 0 also holds smaller values and
     * the last bin larger ones. 1% resolution from 1 us to about 1000 s.
     */
    static constexpr double kLatencyMin = 1e-6;
    static constexpr double kLatencyRatio = 1.01;
    static constexpr size_t kLatencyBins = 2084;

    /** @brief Duplicate-detection window in compact mode (sequence numbers). */
    static constexpr uint64_t kWindow = 1 << 16;

    /** @brief Construct.
     * @param node_id This node's ID
     * @param ipaddr Printf format of node IP addresses (as --tap-ipaddr)
     * @param macaddr Printf format of node MAC addresses (as --tap-macaddr)
     * @param mtu Interface MTU
     */
    SimTraffic(NodeId node_id,
               const std::string& ipaddr,
               const std::string& macaddr,
               size_t mtu);
    virtual ~SimTraffic();

    /** @brief Radio packets delivered here as if written to the tap device. */
    RadioIn<Push> sink;

    /** @brief Network packets produced here as if read from the tap device. */
    NetOut<Push> source;

    /** @brief Add a constant-rate UDP flow to @a dest (kNodeBroadcast for broadcast). */
    void addFlow(uint16_t port, NodeId dest, double rate, size_t size, uint64_t count, double start);

    /** @brief Start all flows. */
    void start(void);

    /** @brief Stop generating. */
    void stop(void);

    /** @brief Get transmit-side flow state. */
    std::vector<Flow> getFlows(void);

    /** @brief Get receive statistics keyed by (source node, port). */
    std::map<std::pair<NodeId, uint16_t>, RecvStats> getReceived(void);

    /** @brief Number of delivered frames that were not synthetic traffic. */
    uint64_t getForeign(void);

    /** @brief Keep per-packet records (default) or bounded summaries.
     * Must be set before traffic arrives.
     */
    void setDetail(bool detail);

    /** @brief True if per-packet records are kept. */
    bool getDetail(void) const { return detail_; }

    /** @brief Build a frame (exposed for tests). */
    std::vector<uint8_t> buildFrame(NodeId dest, uint16_t port, uint64_t seq, size_t size, int64_t tx_ns);

private:
    NodeId node_id_;
    std::string ipaddr_;
    std::string macaddr_;
    size_t mtu_;
    std::shared_ptr<Logger> logger_;

    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
    std::vector<Flow> flows_;
    std::map<std::pair<NodeId, uint16_t>, RecvStats> recv_;
    std::map<std::pair<NodeId, uint16_t>, std::unordered_set<uint64_t>> seen_;
    uint64_t foreign_;
    bool detail_;

    std::thread worker_;

    void send(std::shared_ptr<RadioPacket>&& pkt);

    void receiveCompact(RecvStats &st, uint64_t seq, size_t size, int64_t t_rx, int64_t t_tx);

    void worker(void);
};

}

#endif /* SIM_SIMTRAFFIC_HH_ */
