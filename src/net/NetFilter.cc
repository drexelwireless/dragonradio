#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <cstddef>

#include "NetFilter.hh"

/** @brief Compute broadcast address from address and netmask  */
uint32_t mkBroadcastAddress(uint32_t addr, uint32_t netmask)
{
    return (addr & netmask) | (0xffffffff & ~netmask);
}

NetFilter::NetFilter(std::shared_ptr<Net> net) : net_(net)
{
    struct in_addr in;

    assert(inet_aton(kIntIPNet, &in) != 0);
    int_net_ = ntohl(in.s_addr);

    assert(inet_aton(kIntIPNetmask, &in) != 0);
    int_netmask_ = ntohl(in.s_addr);

    int_broadcast_ = mkBroadcastAddress(int_net_, int_netmask_);

    assert(inet_aton(kExtIPNet, &in) != 0);
    ext_net_ = ntohl(in.s_addr);

    assert(inet_aton(kExtIPNetmask, &in) != 0);
    ext_netmask_ = ntohl(in.s_addr);

    ext_broadcast_ = mkBroadcastAddress(ext_net_, ext_netmask_);
}

bool isEthernetBroadcast(const u_char *host)
{
    return memcmp(host, "\xff\xff\xff\xff\xff\xff", 6) == 0;
}

bool NetFilter::process(std::shared_ptr<NetPacket>& pkt)
{
    if (pkt->size() == 0)
        return false;

    struct ether_header* eth = reinterpret_cast<struct ether_header*>(pkt->data() + sizeof(ExtendedHeader));

    // Node number is last octet of the ethernet MAC address by convention
    NodeId curhop_id = eth->ether_shost[5];
    NodeId nexthop_id = eth->ether_dhost[5];

    // Only transmit IP packets that are either broadcast packets or where we
    // are the source and we know of the destination.
    if (ntohs(eth->ether_type) == ETHERTYPE_IP &&
        (isEthernetBroadcast(eth->ether_dhost) || (curhop_id == net_->getMyNodeId() && net_->contains(nexthop_id)))) {
        struct ip* iph = reinterpret_cast<struct ip*>(pkt->data() + sizeof(ExtendedHeader) + sizeof(struct ether_header));
        in_addr    ip_src;
        in_addr    ip_dst;

        std::memcpy(&ip_src, reinterpret_cast<char*>(iph) + offsetof(struct ip, ip_src), sizeof(ip_src));
        std::memcpy(&ip_dst, reinterpret_cast<char*>(iph) + offsetof(struct ip, ip_dst), sizeof(ip_dst));

        NodeId src_id;
        NodeId dest_id;

        if ((ntohl(ip_src.s_addr) & int_netmask_) == int_net_) {
            // Traffic on the internal network has IP addresses of the form
            // 10.10.10.<SRN>/32
            pkt->setInternalFlag(kIntNet);

            src_id = ntohl(ip_src.s_addr) & 0xff;
            dest_id = ntohl(ip_dst.s_addr) & 0xff;

            if (ntohl(ip_dst.s_addr) == int_broadcast_)
                pkt->setFlag(kBroadcast);
        } else if ((ntohl(ip_src.s_addr) & ext_netmask_) == ext_net_) {
            // Traffic on the external network has IP addresses of the form
            // 192.168.<SRN+100>.0/24
            pkt->setInternalFlag(kExtNet);

            src_id = ((ntohl(ip_src.s_addr) >> 8) & 0xff) - 100;
            dest_id = ((ntohl(ip_dst.s_addr) >> 8) & 0xff) - 100;

            if (ntohl(ip_dst.s_addr) == ext_broadcast_)
                pkt->setFlag(kBroadcast);
        } else
            return false;

        // NOTE: We are only responsible for setting hop/src/dest information
        // here. The pkt->data_len field is set in TunTap when the packet is
        // read from the network, and the sequence number and modulation-related
        // fields are set by the controller
        pkt->curhop = curhop_id;
        pkt->nexthop = nexthop_id;
        pkt->src = src_id;
        pkt->dest = dest_id;

        return true;
    } else
        return false;
}
