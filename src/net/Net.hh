#ifndef NET_HH_
#define NET_HH_

#include <queue>
#include <thread>
#include <stdio.h>

#include "Packet.hh"
#include "SafeQueue.hh"
#include "net/TunTap.hh"

class Net
{
public:
    Net(const std::string& tap_name,
        const std::string& ip_fmt,
        const std::string& mac_fmt,
        NodeId nodeId);
    ~Net();

    /** @brief Halt packet processing. */
    void stop(void);

    /** @breif Get this node's ID */
    NodeId getMyNodeId(void);

    /** @brief Get the number of nodes in the network */
    size_t getNumNodes(void);

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
    std::unique_ptr<TunTap> tt;

    /** @brief This node's ID */
    NodeId myNodeId;

    /** @brief The nodes in the network */
    std::vector<NodeId> nodes;

    /** @brief Current packet id */
    PacketId curPacketId;

    /** @brief Flag indicating if we should stop processing packets */
    bool done;

    /** @brief Thread running recvWorker */
    std::thread recvThread;

    /** @brief Read packets from tun/tap and queue them in recvQueue */
    void recvWorker(void);

    /** @brief Radio packets received from the network */
    SafeQueue<std::unique_ptr<NetPacket>> recvQueue;
};

#endif /* NET_HH_ */
