// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <cstddef>

#include "logging.hh"
#include "net/NetFilter.hh"
#include "util/net.hh"

bool NetFilter::process(std::shared_ptr<NetPacket>& pkt)
{
    if (pkt->size() == 0) {
        logNet(LOGDEBUG, "dropped size zero packet");
        return false;
    }

    struct ether_header* eth = reinterpret_cast<struct ether_header*>(pkt->data() + sizeof(ExtendedHeader));

    // Node number is last octet of the ethernet MAC address by convention
    NodeId curhop_id = eth->ether_shost[5];
    NodeId nexthop_id = eth->ether_dhost[5];

    // Only transmit IP packets that are either broadcast packets or where we
    // are the source and we know of the destination.
    if (ntohs(eth->ether_type) == ETHERTYPE_IP &&
        (isEthernetBroadcast(eth->ether_dhost) || (curhop_id == nhood_->getThisNodeId() && nhood_->contains(nexthop_id)))) {
        struct ip* iph = reinterpret_cast<struct ip*>(pkt->data() + sizeof(ExtendedHeader) + sizeof(struct ether_header));
        in_addr    ip_src;
        in_addr    ip_dst;

        std::memcpy(&ip_src, reinterpret_cast<char*>(iph) + offsetof(struct ip, ip_src), sizeof(ip_src));
        std::memcpy(&ip_dst, reinterpret_cast<char*>(iph) + offsetof(struct ip, ip_dst), sizeof(ip_dst));

        NodeId   src_id;
        NodeId   dest_id;
        uint32_t src_addr = ntohl(ip_src.s_addr);
        uint32_t dest_addr = ntohl(ip_dst.s_addr);

        if ((src_addr & int_netmask_) == int_net_) {
            // Traffic on the internal network has IP addresses of the form
            // 10.10.10.<SRN>/32
            src_id = src_addr & 0xff;
            dest_id = dest_addr & 0xff;

            if (dest_addr == int_broadcast_)
                pkt->hdr.nexthop = kNodeBroadcast;
        } else if ((src_addr & ext_netmask_) == ext_net_) {
            // Traffic on the external network has IP addresses of the form
            // 192.168.<SRN+100>.0/24
            src_id = ((src_addr >> 8) & 0xff) - 100;
            dest_id = ((dest_addr >> 8) & 0xff) - 100;

            if (dest_addr == ext_broadcast_)
                pkt->hdr.nexthop = kNodeBroadcast;
        } else {
            logNet(LOGDEBUG, "dropped IP packet from unknown subnet %d.%d.%d.%d",
                (src_addr >> 24) & 0xff,
                (src_addr >> 16) & 0xff,
                (src_addr >> 8) & 0xff,
                src_addr & 0xff);
            return false;
        }

        // NOTE: We are only responsible for setting hop/src/dest information
        // here. The pkt->ehdr().data_len field is set in TunTap when the packet is
        // read from the network, and the sequence number and modulation-related
        // fields are set by the controller
        pkt->hdr.curhop = curhop_id;
        pkt->hdr.nexthop = nexthop_id;
        pkt->ehdr().src = src_id;
        pkt->ehdr().dest = dest_id;

        // Cache payload size
        pkt->payload_size = pkt->getPayloadSize();

        logNet(LOGDEBUG-1, "Read %lu bytes from %u to %u",
            (unsigned long) pkt->ehdr().data_len,
            (unsigned) pkt->hdr.curhop,
            (unsigned) pkt->hdr.nexthop);

        return true;
    } else {
        logNet(LOGDEBUG, "dropped unknown packet: ether_type=0x%x; shost=%u; dhost=%u",
            ntohs(eth->ether_type),
            eth->ether_shost[5],
            eth->ether_dhost[5]);
        return false;
    }
}
