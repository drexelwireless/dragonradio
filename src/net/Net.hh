#ifndef NET_HH_
#define NET_HH_

#include <math.h>

#include <map>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Packet.hh"
#include "SafeQueue.hh"
#include "net/TunTap.hh"

struct Node {
    Node(NodeId id);
    ~Node();

    /** @brief Node ID */
    NodeId id;

    /** @brief Current packet sequence number for this destination */
    Seq seq;

    /** @brief Modulation scheme */
    modulation_scheme ms;

    /** @brief Data validity check */
    crc_scheme check;

    /** @brief Inner FEC */
    fec_scheme fec0;

    /** @brief Outer FEC */
    fec_scheme fec1;

    /** @brief Soft TX gain (multiplicative factor) */
    float g;

    /** @brief Get soft TX gain (dB). */
    float getSoftTXGain(void)
    {
        return 20.0*logf(g)/logf(10.0);
    }

    /** @brief Set soft TX gain.
     * @param dB The soft gain (dB).
     */
    void setSoftTXGain(float dB)
    {
        g = powf(10.0f, dB/20.0f);
    }

    /** @brief Desired soft gain in dBFS. Defaults to 0.0. */
    /** This is our soft TX gain goal, in dBFS. Note well the units! We use this
     * value to dynamically set the multiplicative soft gain based on generated
     * IQ values.
     */
    float desired_soft_tx_gain;

    /** @brief Fraction of unclipped IQ values. Defaults to 0.999. */
    /** This sets the fraction of values guaranteed to be unclipped when the
     * soft TX gain is automatically determined.
     */
    float desired_soft_tx_gain_clip_frac;

    /** @brief Set to force recalculation of soft TX gain based on
     * desired_soft_tx_gain next time a packet to this node is modulated.
     */
    bool recalc_soft_tx_gain;

    /** @brief Get desired soft TX gain (dBFS). */
    float getDesiredSoftTXGain(void)
    {
        return desired_soft_tx_gain;
    }

    /** @brief Set desired soft TX gain.
     * @param dBFs The soft gain (dBFS).
     */
    void setDesiredSoftTXGain(float dBFS)
    {
        desired_soft_tx_gain = dBFS;
        recalc_soft_tx_gain = true;
    }

    /** @brief ACK delay in seconds */
    double ack_delay;

    /** @brief Packet re-transmit delay in seconds */
    double retransmission_delay;
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
    void addNode(NodeId nodeId);

    /** @brief Return true if we want a packet sent to this destination. */
    bool wantPacket(NodeId dest);

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
