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
    Node();
    ~Node();

    /** @brief Soft TX gain (multiplicative factor) */
    double g;

    /** @brief Modulation scheme */
    modulation_scheme ms;

    /** @brief Data validity check */
    crc_scheme check;

    /** @brief Inner FEC */
    fec_scheme fec0;

    /** @brief Outer FEC */
    fec_scheme fec1;

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
};

class Net
{
public:
    using map_type = std::map<NodeId, Node>;

    Net(const std::string& tap_name,
        const std::string& ip_fmt,
        const std::string& mac_fmt,
        NodeId nodeId);
    ~Net();

    Net(const Net&) = delete;
    Net(Net&&) = delete;

    Net& operator=(const Net&) = delete;
    Net& operator=(Net&&) = delete;

    /** @brief Start packet processing. */
    void start(void);

    /** @brief Halt packet processing. */
    void stop(void);

    /** @breif Get this node's ID */
    NodeId getMyNodeId(void);

    /** @brief Get the number of nodes in the network */
    map_type::size_type size(void);

    /** @brief Get the number of nodes in the network */
    map_type::size_type count(NodeId nodeId);

    /** @brief Get the entry for a particular node in the network */
    Node& operator[](NodeId nodeid);

    /** @brief Add a node to the network */
    void addNode(NodeId nodeId);

    /** @brief Receive a packet from the network */
    std::unique_ptr<NetPacket> recvPacket(void);

    /** @brief Return true if we want a packet sent to this destination. */
    bool wantPacket(NodeId dest);

    /** @brief Send a packet to the network. */
    void send(std::unique_ptr<RadioPacket> pkt);

private:
    /** @brief Our tun/tap interface */
    std::unique_ptr<TunTap> tuntapdev_;

    /** @brief This node's ID */
    NodeId my_node_id_;

    /** @brief The nodes in the network */
    std::map<NodeId, Node> nodes_;

    /** @brief Current packet id */
    PacketId cur_pkt_id_;

    /** @brief Flag indicating if we should stop processing packets */
    bool done_;

    /** @brief Thread running recvWorker */
    std::thread recv_thread_;

    /** @brief Radio packets received from the network */
    SafeQueue<std::unique_ptr<NetPacket>> recv_q_;

    /** @brief Read packets from tun/tap and queue them in recvQueue */
    void recvWorker(void);
};

#endif /* NET_HH_ */
