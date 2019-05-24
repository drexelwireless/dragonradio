#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>
#include <functional>

#include "RadioConfig.hh"
#include "Util.hh"
#include "net/Net.hh"

using namespace std::placeholders;

const char *kIntIPNet = "10.10.10.0";

const char *kIntIPNetmask = "255.255.255.0";

const char *kExtIPNet = "192.168.0.0";

const char *kExtIPNetmask = "255.255.0.0";

Node::Node(NodeId id)
  : id(id)
  , is_gateway(false)
  , can_transmit(true)
  , seq(0)
  , g(1.0)
  , ack_delay(rc.arq_ack_delay)
  , retransmission_delay(rc.arq_retransmission_delay)
  , short_per(1)
  , long_per(1)
{
}

Net::Net(std::shared_ptr<TunTap> tuntap,
         NodeId nodeId)
  : tuntap_(tuntap)
  , my_node_id_(nodeId)
{
}

std::optional<NodeId> Net::getTimeMaster(void)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::optional<NodeId>       master;

    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->second.is_gateway && (!master || it->first < *master))
            master = it->first;
    }

    return master;
}

Node& Net::operator[](NodeId nodeid)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    if (nodes_.count(nodeid) == 0)
        return addNode_(nodeid);
    else
        return nodes_.at(nodeid);
}

Node &Net::addNode(NodeId nodeId)
{
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    return addNode_(nodeId);
}

Node &Net::addNode_(NodeId nodeId)
{
    auto entry = nodes_.emplace(std::piecewise_construct,
                                std::forward_as_tuple(nodeId),
                                std::forward_as_tuple(nodeId));

    // If the entry is new, add an ARP entry for it
    if (entry.second && nodeId != my_node_id_)
        tuntap_->addARPEntry(nodeId);

    return entry.first->second;
}
