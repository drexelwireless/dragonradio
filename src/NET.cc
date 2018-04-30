// DWSL - full radio stack

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>

#include "NET.hh"

NET::NET(const std::string& tap_name, NodeId nodeId, const std::vector<NodeId>& nodes)
  : nodeId(nodeId),
    numNodes(nodes.size()),
    curPacketId(0),
    done(false)
{
    printf("Creating tap interface %s\n", tap_name.c_str());

    tt = std::make_unique<TunTap>(tap_name, nodeId, nodes);

    recvThread = std::thread(&NET::recvWorker, this);
    sendThread = std::thread(&NET::sendWorker, this);
}

NET::~NET()
{
    printf("Closing tap interface\n");
}

void NET::stop(void)
{
    done = true;
    recvQueue.stop();
    sendQueue.stop();

    if (recvThread.joinable())
        recvThread.join();

    if (sendThread.joinable())
        sendThread.join();
}

NodeId NET::getNodeId(void)
{
    return nodeId;
}

unsigned int NET::getNumNodes(void)
{
    return numNodes;
}

std::unique_ptr<NetPacket> NET::recvPacket(void)
{
    std::unique_ptr<NetPacket> pkt;

    recvQueue.pop(pkt);

    return pkt;
}

bool NET::wantPacket(NodeId dest)
{
    return dest == nodeId;
}

/** Maximum radio packet size. Really 1500 (MTU) + 14 (size of Ethernet header),
    which we should properly calculate at some point. */
const size_t MAX_PKT_SIZE = 2000;

void NET::recvWorker(void)
{
    while (!done) {
        auto    pkt = std::make_unique<NetPacket>(MAX_PKT_SIZE);
        ssize_t count;

        count = tt->cread(&(pkt->payload)[0], pkt->payload.size());
        pkt->payload.resize(count);

        if (pkt->payload.size() > 0) {
            struct ip* ip = reinterpret_cast<struct ip*>(&(pkt->payload)[0] + sizeof(struct ether_header));
            in_addr    ip_dst;

            std::memcpy(&ip_dst, reinterpret_cast<char*>(ip) + offsetof(struct ip, ip_dst), sizeof(ip_dst));

            pkt->src = nodeId;

            // Destination node is last octet of the IP address by convention
            pkt->dest = ntohl(ip_dst.s_addr) & 0xff;

            pkt->pkt_id = curPacketId++;

            recvQueue.push(std::move(pkt));
        }
    }
}

void NET::sendWorker(void)
{
    std::unique_ptr<RadioPacket> pkt;

    while (!done) {
        if (sendQueue.pop(pkt)) {
            printf("Written %lu bytes (PID %u) from %u\n",
                (unsigned long) pkt->payload.size(),
                (unsigned int) pkt->pkt_id,
                (unsigned int) pkt->src);

            tt->cwrite(&(pkt->payload)[0], pkt->payload.size());
        }
    }
}
