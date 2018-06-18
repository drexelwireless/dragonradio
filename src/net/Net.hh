#ifndef NET_HH_
#define NET_HH_

#include <math.h>

#include <map>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Estimator.hh"
#include "Packet.hh"
#include "SafeQueue.hh"
#include "net/TunTap.hh"

struct Node {
    Node(NodeId id, TXParams *tx_params);
    ~Node();

    /** @brief Node ID */
    NodeId id;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Current packet sequence number for this destination */
    Seq seq;

    /** @brief Modulation index */
    size_t modidx;

    /** @brief TX parameters */
    /** This points to the TX params used to modulate data sent to this node.
     * It is non-const because we are allowed to update the g_0dBFS field.
     */
    TXParams *tx_params;

    /** @brief Multiplicative TX gain as measured against 0 dBFS. */
    float g;

    /** @brief ACK delay in seconds */
    double ack_delay;

    /** @brief Packet re-transmit delay in seconds */
    double retransmission_delay;

    /** @brief Short-term packet error rate */
    EMA<double> short_per;

    /** @brief Long-term packet error rate */
    EMA<double> long_per;

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

    /** @brief Update a NetPacket with our TXParams. */
    void updateNetPacketTXParams(NetPacket &pkt)
    {
        pkt.tx_params = tx_params;
        pkt.g = tx_params->g_0dBFS.getValue() * g;
    }
};

class Net : public Element
{
public:
    using map_type = std::map<NodeId, Node>;

    Net(std::shared_ptr<TunTap> tuntap,
        NodeId nodeId);
    ~Net();

    Net(const Net&) = delete;
    Net(Net&&) = delete;

    Net& operator=(const Net&) = delete;
    Net& operator=(Net&&) = delete;

    /** @breif Get this node's ID */
    NodeId getMyNodeId(void);

    /** @brief Get the number of nodes in the network */
    map_type::size_type size(void);

    /** @brief Return true if node is in the network, false otherwise */
    bool contains(NodeId nodeId);

    /** @brief Return an iterator to the beginning of nodes. */
    map_type::iterator begin(void);

    /** @brief Return an iterator to the end of nodes. */
    map_type::iterator end(void);

    /** @brief Get the entry for a particular node in the network */
    Node& operator[](NodeId nodeid);

    /** @brief Add a node to the network */
    Node &addNode(NodeId nodeId);

    /** @brief TX params */
    std::vector<TXParams> tx_params;

private:
    /** @brief Our tun/tap interface */
    std::shared_ptr<TunTap> tuntap_;

    /** @brief This node's ID */
    NodeId my_node_id_;

    /** @brief The nodes in the network */
    std::map<NodeId, Node> nodes_;

    /** @brief Mutex protecting nodes in the network */
    std::mutex nodes_mutex_;
};

#endif /* NET_HH_ */
