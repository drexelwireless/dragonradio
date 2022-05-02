// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cstddef>
#include <cstring>
#include <functional>

#include "Neighborhood.hh"

std::optional<NodeId> Neighborhood::getTimeMaster(void) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<NodeId>       master;

    for (auto it = neighbors_.begin(); it != neighbors_.end(); ++it) {
        if (it->second->is_gateway && (!master || it->first < *master))
            master = it->first;
    }

    return master;
}

std::shared_ptr<Node> Neighborhood::addNeighbor(NodeId node_id)
{
    std::shared_ptr<Node> node;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto&                 [it, created] = neighbors_.try_emplace(node_id, nullptr);

        // If the entry is new, construct the shared_ptr. We pass nullptr above
        // to avoid creating a shared_ptr even if the entry already exists.
        if (created) {
            node = std::make_shared<Node>(node_id);
            it->second = node;
        } else
            return it->second;
    }

    // We only reach this point if the node was created. We go through this
    // rigamarole so that we can perform the following operations without
    // holding the mutex.

    // Add ARP entry
    tuntap_->addARPEntry(node_id);

    // Notify listeners
    notify([=](NeighborhoodListener &listener) { listener.neighborAdded(node); });

    return node;
}

void Neighborhood::addNeighbor(const std::shared_ptr<Node> &node)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto&                 [it, created] = neighbors_.try_emplace(node->id, node);

        if (!created)
            return;
    }

    // Add ARP entry
    tuntap_->addARPEntry(node->id);

    // Notify listeners
    notify([=](NeighborhoodListener &listener) { listener.neighborAdded(node); });
}

void Neighborhood::removeNeighbor(NodeId node_id)
{
    std::shared_ptr<Node> node;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = neighbors_.find(node_id);

        if (it != neighbors_.end()) {
            node = it->second;
            neighbors_.erase(it);
        }
    }

    // node will be non-empty iff we deleted a node
    if (node) {
        // Delete ARP entry
        tuntap_->deleteARPEntry(node_id);

        // Notify listeners
        notify([=](NeighborhoodListener &listener) { listener.neighborRemoved(node); });
    }
}

void Neighborhood::addGateway(const std::shared_ptr<Node> &node)
{
    // Notify listeners
    notify([=](NeighborhoodListener &listener) { listener.gatewayAdded(node); });
}
