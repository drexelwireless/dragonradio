// DWSL - full radio stack

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>

#include "NET.hh"

NET::NET(const std::string& tap_name, NodeId nodeId, const std::vector<NodeId>& nodes)
  : nodeId(nodeId),
    numNodes(nodes.size()),
    curPacketId(0),
    done(false)
{
    printf("Creating NETWORK\n");

    tt.reset(new TunTap(tap_name, nodeId, nodes));

    recvThread = std::thread(&NET::recvWorker, this);
    sendThread = std::thread(&NET::sendWorker, this);
}

NET::~NET()
{
    printf("Closing Network\n");
}

NodeId NET::getNodeId(void)
{
    return nodeId;
}

unsigned int NET::getNumNodes(void)
{
    return numNodes;
}

std::unique_ptr<RadioPacket> NET::recvPacket(void)
{
    std::unique_ptr<RadioPacket> pkt;

    recvQueue.pop(pkt);

    return pkt;
}

ssize_t NET::sendPacket(void* data, size_t n)
{
    std::unique_ptr<NetPacket> pkt(new NetPacket(n));

    memcpy(&(*pkt)[0], data, n);
    sendQueue.push(std::move(pkt));

    return n;
}

void NET::join(void)
{
    done = true;
    recvThread.join();
    sendThread.join();
}

/** Maximum radio packet size. Really 1500 (MTU) + 14 (size of Ethernet header),
    which we should properly calculate ate some point. */
const size_t MAX_PKT_SIZE = 2000;

void NET::recvWorker(void)
{
    while (!done) {
        std::unique_ptr<RadioPacket> pkt(new RadioPacket(MAX_PKT_SIZE));

        pkt->payload_len = tt->cread(&(pkt->payload)[0], pkt->payload.size());

        if (pkt->payload_len > 0) {
            struct ip* ip = reinterpret_cast<struct ip*>(&(pkt->payload)[0] + sizeof(struct ether_header));

            // Destination node is last octet of the IP address by convention
            pkt->dest = ntohl(ip->ip_dst.s_addr) & 0xff;

            pkt->packet_id = curPacketId++;

            recvQueue.push(std::move(pkt));
        }
    }
}

void NET::sendWorker(void)
{
    while (!done) {
        std::unique_ptr<NetPacket> pkt;

        sendQueue.pop(pkt);

        tt->cwrite(&(*pkt)[0], pkt->size());
    }
}
