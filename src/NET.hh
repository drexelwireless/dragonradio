// DWSL - full radio stack

#ifndef NET_HH_
#define NET_HH_

#include <TunTap.hh>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Node.hh"
#include "Packet.hh"
#include "SafeQueue.hh"

class NET
{
public:
    NET(const std::string& tap_name, NodeId nodeId, const std::vector<NodeId>& nodes);
    ~NET();

    /** @brief Halt packet processing. */
    void stop(void);

    /** @breif Get this node's ID */
    NodeId getNodeId(void);

    /** @brief Get the number of nodes in the network */
    unsigned int getNumNodes(void);

    /** @brief Receive a packet from the network */
    std::unique_ptr<NetPacket> recvPacket(void);

    /** @brief Return true if we want a packet sent to this destination. */
    bool wantPacket(NodeId dest);

    /** @brief Send a packet to the network */
    void sendPacket(std::unique_ptr<RadioPacket> pkt);

private:
    /** @brief Our tun/tap interface */
    std::unique_ptr<TunTap> tt;

    /** @brief This node's ID */
    NodeId nodeId;

    /** @brief The number of nodes in the network */
    unsigned int numNodes;

    /** @brief Current packet id */
    PacketId curPacketId;

    /** @brief Flag indicating if we should stop processing packets */
    bool done;

    /** @brief Thread running recvWorker */
    std::thread recvThread;

    /** @brief Read packets from tun/tap and queue them in recvQueue */
    void recvWorker(void);

    /** @brief Thread running sendWorker */
    std::thread sendThread;

    /** @brief Read packets from sendQueue and send them on tun/tap */
    void sendWorker(void);

    /** @brief Radio packets received from the network */
    SafeQueue<std::unique_ptr<NetPacket>> recvQueue;

    /** @brief Network packets to send to the network */
    SafeQueue<std::unique_ptr<RadioPacket>> sendQueue;
};

#endif
