// DWSL - full radio stack

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "NET.hh"

NET::NET(const std::string& tap_name, NodeId node_id, const std::vector<unsigned char>& nodes_in_net)
{
    printf("Creating NETWORK\n");
    this->node_id = node_id;
    this->num_nodes_in_net = nodes_in_net.size();
    this->tt = new TunTap(tap_name, node_id, nodes_in_net);
    this->continue_reading = true;
    this->readThread = std::thread(&NET::readPackets,this);
    this->txed_packets = 0;
    this->curr_packet_id = 0;
}

NET::~NET()
{
    continue_reading = false;
    printf("Closing Network\n");
    delete tt;
}

/** Maximum radio packet size. Really 1500 (MTU) + 14 (size of Ethernet header),
    which we should properly calculate ate some point. */
const size_t MAX_PKT_SIZE = 2000;

void NET::readPackets()
{
    while (continue_reading)
    {
        std::unique_ptr<RadioPacket> pkt(new RadioPacket(MAX_PKT_SIZE));

        pkt->payload_len = tt->cread(&(pkt->payload)[0], pkt->payload.size());

        if (pkt->payload_len > 0) {
            struct ip* ip = reinterpret_cast<struct ip*>(&(pkt->payload)[0] + sizeof(struct ether_header));

            // Destination node is last octet of the IP address by convention
            pkt->dest = ntohl(ip->ip_dst.s_addr) & 0xff;

            pkt->packet_id = curr_packet_id++;

            tx_packets.push(std::move(pkt));
        }
    }
}

std::unique_ptr<RadioPacket> NET::get_next_packet()
{
    if (tx_packets.empty())
        return nullptr;

    std::unique_ptr<RadioPacket> it = std::move(tx_packets.front());
    tx_packets.pop();

    txed_packets++;
    return it;
}
