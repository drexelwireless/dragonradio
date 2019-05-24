#ifndef NET_HH_
#define NET_HH_

#include <math.h>

#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "Packet.hh"
#include "SafeQueue.hh"
#include "net/TunTap.hh"
#include "stats/Estimator.hh"

/** @brief A sprintf-style format string for internal network tun/tap IP
 * addresses.
 */
const std::string kIntIPFmt = "10.10.10.%d";

/** @brief A sprintf-style format string for tun/tap MAC addresses */
const std::string kMACFmt = "c6:ff:ff:ff:ff:%02x";

/** @brief Internal IP network. */
extern const char *kIntIPNet;

/** @brief Internal IP network mask. */
extern const char *kIntIPNetmask;

/** @brief External IP network. */
extern const char *kExtIPNet;

/** @brief External IP network mask. */
extern const char *kExtIPNetmask;

/** @brief Vector of pairs of timestamps. */
/** The first timestamp is the transmitter's timestamp, and the second timestamp
 * is the local time at which the timestamp was received.
 */
using timestamp_vector = std::vector<std::pair<MonoClock::time_point, MonoClock::time_point>>;

struct Node {
    explicit Node(NodeId id);

    Node() = delete;

    ~Node() = default;

    /** @brief Node ID */
    const NodeId id;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Flag indicating whether or not this node can transmit */
    bool can_transmit;

    /** @brief Multiplicative TX gain as measured against 0 dBFS. */
    float g;

    /** @brief ACK delay in seconds */
    double ack_delay;

    /** @brief Packet re-transmit delay in seconds */
    double retransmission_delay;

    /** @brief Short-term packet error rate */
    WindowedMean<double> short_per;

    /** @brief Long-term packet error rate */
    WindowedMean<double> long_per;

    /** @brief Timestamps received from this node */
    timestamp_vector timestamps;

    /** @brief Set soft TX gain.
     * @param dB The soft gain (dBFS).
     */
    void setSoftTXGain(float dB)
    {
        g = powf(10.0f, dB/20.0f);
    }

    /** @brief Get soft TX gain (dBFS). */
    float getSoftTXGain(void) const
    {
        return 20.0*logf(g)/logf(10.0);
    }
};

class Net
{
public:
    using NodeMap = std::map<NodeId, std::shared_ptr<Node>>;

    Net(std::shared_ptr<TunTap> tuntap,
        NodeId nodeId);
    Net() = delete;

    ~Net() = default;

    Net(const Net&) = delete;
    Net(Net&&) = delete;

    Net& operator=(const Net&) = delete;
    Net& operator=(Net&&) = delete;

    /** @brief Get this node's ID */
    NodeId getMyNodeId(void) const
    {
        return my_node_id_;
    }

    /** @brief Return true if node is in the network, false otherwise */
    bool contains(NodeId nodeId)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        return nodes_.find(nodeId) != nodes_.end();
    }

    /** @brief Get nodes */
    /** @return A copy of the current node map. */
    NodeMap getNodes(void)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        return nodes_;
    }

    /** @brief Get the entry for this node */
    Node &me(void)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        return *nodes_.at(getMyNodeId());
    }

    /** @brief Get the entry for a particular node in the network */
    Node& operator[](NodeId nodeId)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto                        entry = nodes_.try_emplace(nodeId, nullptr);

        // If the entry is new, construct the shared_ptr. We pass nullptr above
        // to avoid creating a shared_ptr even if the entry already exists.
        if (entry.second) {
            entry.first->second = std::make_shared<Node>(nodeId);

            // Add ARP entry
            if (nodeId != my_node_id_)
                tuntap_->addARPEntry(nodeId);
        }

        return *(entry.first->second);
    }

    /** @brief Apply a function to each node */
    void foreach(std::function<void(Node&)> f)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
            f(*(it->second));
    }

    /** @brief Get the node that is the time master */
    std::optional<NodeId> getTimeMaster(void);

private:
    /** @brief Our tun/tap interface */
    std::shared_ptr<TunTap> tuntap_;

    /** @brief This node's ID */
    const NodeId my_node_id_;

    /** @brief Mutex protecting nodes in the network */
    std::mutex nodes_mutex_;

    /** @brief The nodes in the network */
    NodeMap nodes_;
};

#endif /* NET_HH_ */
