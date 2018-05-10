#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>

#include "RadioConfig.hh"
#include "Util.hh"
#include "net/Net.hh"

Node::Node(NodeId id) :
    id(id),
    ms(rc->ms),
    check(rc->check),
    fec0(rc->fec0),
    fec1(rc->fec1)
{
    setSoftTXGain(rc->soft_txgain);
}

Node::~Node()
{
}

Net::Net(const std::string& tap_name,
         const std::string& ip_fmt,
         const std::string& mac_fmt,
         NodeId nodeId) :
    my_node_id_(nodeId),
    cur_pkt_id_(0),
    done_(true)
{
    if (rc->verbose)
        printf("Creating tap interface %s\n", tap_name.c_str());

    tuntapdev_ = std::make_unique<TunTap>(tap_name, false, 1500, ip_fmt, mac_fmt, nodeId);

    start();
}

Net::~Net()
{
    stop();
}

void Net::start(void)
{
    if (done_) {
        done_ = false;
        recv_thread_ = std::thread(&Net::recvWorker, this);
    }
}

void Net::stop(void)
{
    done_ = true;
    recv_q_.stop();

    if (recv_thread_.joinable())
        recv_thread_.join();
}

NodeId Net::getMyNodeId(void)
{
    return my_node_id_;
}

Net::map_type::size_type Net::size(void)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    return nodes_.size();
}

bool Net::contains(NodeId nodeid)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    return nodes_.count(nodeid) == 1;
}

Node& Net::operator[](NodeId nodeid)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    return nodes_.at(nodeid);
}

void Net::addNode(NodeId nodeId)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    nodes_.emplace(nodeId, Node(nodeId));

    if (nodeId != my_node_id_)
        tuntapdev_->add_arp_entry(nodeId);
}

std::unique_ptr<NetPacket> Net::recvPacket(void)
{
    std::unique_ptr<NetPacket> pkt;

    recv_q_.pop(pkt);

    return pkt;
}

bool Net::wantPacket(NodeId dest)
{
    return dest == my_node_id_;
}

void Net::send(std::unique_ptr<RadioPacket> pkt)
{
    tuntapdev_->cwrite(pkt->data(), pkt->size());

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
    while (!done_) {
        auto    pkt = std::make_unique<NetPacket>(MAX_PKT_SIZE);
        ssize_t count;

        count = tuntapdev_->cread(pkt->data(), pkt->size());
        pkt->resize(count);

        if (pkt->size() > 0) {
            struct ip* ip = reinterpret_cast<struct ip*>(pkt->data() + sizeof(struct ether_header));
            in_addr    ip_dst;

            std::memcpy(&ip_dst, reinterpret_cast<char*>(ip) + offsetof(struct ip, ip_dst), sizeof(ip_dst));

            // Destination node is last octet of the IP address by convention
            NodeId destId = ntohl(ip_dst.s_addr) & 0xff;

            if (contains(destId)) {
                Node&  dest = (*this)[destId];

                pkt->src = my_node_id_;
                pkt->dest = destId;

                pkt->pkt_id = cur_pkt_id_++;
                pkt->check = dest.check;
                pkt->fec0 = dest.fec0;
                pkt->fec1 = dest.fec1;
                pkt->ms = dest.ms;
                pkt->g = dest.g;

                recv_q_.push(std::move(pkt));
            }
        }
    }
}
