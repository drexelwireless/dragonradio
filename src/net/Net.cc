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

Node::Node(NodeId id) :
    id(id),
    seq(0),
    ms(rc->ms),
    check(rc->check),
    fec0(rc->fec0),
    fec1(rc->fec1),
    desired_soft_tx_gain(0.0),
    desired_soft_tx_gain_clip_frac(0.999),
    recalc_soft_tx_gain(false),
    ack_delay(100e-3),
    retransmission_delay(500e-3)
{
    setSoftTXGain(rc->soft_txgain);
}

Node::~Node()
{
}

Net::Net(std::shared_ptr<TunTap> tuntap,
         NodeId nodeId) :
    tuntap_(tuntap),
    my_node_id_(nodeId)
{
}

Net::~Net()
{
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

Net::map_type::iterator Net::begin(void)
{
    return nodes_.begin();
}

Net::map_type::iterator Net::end(void)
{
    return nodes_.end();
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
        tuntap_->addARPEntry(nodeId);
}
