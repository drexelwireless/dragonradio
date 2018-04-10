// DWSL - full radio stack

#ifndef NET_HH_
#define NET_HH_

#include <TunTap.hh>
#include <queue>
#include <thread>
#include <stdio.h>

#include "Node.hh"
#include "RadioPacket.hh"
#include "SafeQueue.hh"

typedef std::vector<char> NetPacket;

class NET
{
public:
    NET(const std::string& tap_name, NodeId nodeId, const std::vector<NodeId>& nodes);
    ~NET();

    /** Get this node's ID */
    NodeId getNodeId(void);

    /** Get the number of nodes in the network */
    unsigned int getNumNodes(void);

    /** Receive a radio packet from the network */
    std::unique_ptr<RadioPacket> recvPacket(void);

    /** Send a network packet to the network */
    ssize_t sendPacket(void* data, size_t n);

    /** Terminate packet processing threads */
    void stop(void);

private:
    /** Our tun/tap interface */
    std::unique_ptr<TunTap> tt;

    /** This node's ID */
    NodeId nodeId;

    /** The number of nodes in the network */
    unsigned int numNodes;

    /** Current packet id */
    PacketId curPacketId;

    /** Flag indicating if we should stop processing packets */
    bool done;

    /** Thread running recvWorker */
    std::thread recvThread;

    /** Read packets from tun/tap and queue them in recvQueue */
    void recvWorker(void);

    /** Thread running sendWorker */
    std::thread sendThread;

    /** Read packets from sendQueue and send them on tun/tap */
    void sendWorker(void);

    /** Radio packets received from the network */
    SafeQueue<std::unique_ptr<RadioPacket>> recvQueue;

    /** Network packets to send to the network */
    SafeQueue<std::unique_ptr<NetPacket>> sendQueue;
};

#endif
