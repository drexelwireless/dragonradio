#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>

#include "RadioConfig.hh"
#include "Util.hh"
#include "net/Net.hh"

Net::Net(const std::string& tap_name,
         const std::string& ip_fmt,
         const std::string& mac_fmt,
         NodeId nodeId) :
    myNodeId(nodeId),
    curPacketId(0),
    done(false)
{
    if (rc->verbose)
        printf("Creating tap interface %s\n", tap_name.c_str());

    tt = std::make_unique<TunTap>(tap_name, false, 1500, ip_fmt, mac_fmt, nodeId);

    recvThread = std::thread(&Net::recvWorker, this);
}

Net::~Net()
{
    if (rc->verbose)
        printf("Closing tap interface\n");
}

void Net::stop(void)
{
    done = true;
    recvQueue.stop();

    if (recvThread.joinable())
        recvThread.join();
}

NodeId Net::getMyNodeId(void)
{
    return myNodeId;
}

size_t Net::getNumNodes(void)
{
    return nodes.size();
}

void Net::addNode(NodeId nodeId)
{
    nodes.emplace_back(nodeId);

    if (nodeId != this->myNodeId)
        tt->add_arp_entry(nodeId);
}

std::unique_ptr<NetPacket> Net::recvPacket(void)
{
    std::unique_ptr<NetPacket> pkt;

    recvQueue.pop(pkt);

    return pkt;
}

bool Net::wantPacket(NodeId dest)
{
    return dest == myNodeId;
}

void Net::send(std::unique_ptr<RadioPacket> pkt)
{
    tt->cwrite(pkt->data(), pkt->size());

    if (rc->verbose)
        printf("Written %lu bytes (PID %u) from %u\n",
            (unsigned long) pkt->size(),
            (unsigned int) pkt->pkt_id,
            (unsigned int) pkt->src);
}

/** Maximum radio packet size. Really 1500 (MTU) + 14 (size of Ethernet header),
    which we should properly calculate at some point. */
const size_t MAX_PKT_SIZE = 2000;

void Net::recvWorker(void)
{
    while (!done) {
        auto    pkt = std::make_unique<NetPacket>(MAX_PKT_SIZE);
        ssize_t count;

        count = tt->cread(pkt->data(), pkt->size());
        pkt->resize(count);

        if (pkt->size() > 0) {
            struct ip* ip = reinterpret_cast<struct ip*>(pkt->data() + sizeof(struct ether_header));
            in_addr    ip_dst;

            std::memcpy(&ip_dst, reinterpret_cast<char*>(ip) + offsetof(struct ip, ip_dst), sizeof(ip_dst));

            pkt->src = myNodeId;

            // Destination node is last octet of the IP address by convention
            pkt->dest = ntohl(ip_dst.s_addr) & 0xff;

            pkt->pkt_id = curPacketId++;

            recvQueue.push(std::move(pkt));
        }
    }
}
