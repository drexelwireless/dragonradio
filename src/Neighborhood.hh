// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NEIGHBORHOOD_HH_
#define NEIGHBORHOOD_HH_

#include <math.h>

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "Node.hh"
#include "Packet.hh"
#include "net/TunTap.hh"

class Neighborhood
{
public:
    using NodeMap = std::map<NodeId, std::shared_ptr<Node>>;

    using new_node_callback_t = std::function<void(const std::shared_ptr<Node>&)>;

    Neighborhood() = delete;

    Neighborhood(std::shared_ptr<TunTap> tuntap,
                 NodeId this_node_id)
      : me(std::make_shared<Node>(this_node_id))
      , tuntap_(tuntap)
      , nodes_({ {this_node_id, me} })
    {
    }

    ~Neighborhood() = default;

    Neighborhood(const Neighborhood&) = delete;
    Neighborhood(Neighborhood&&) = delete;

    Neighborhood& operator=(const Neighborhood&) = delete;
    Neighborhood& operator=(Neighborhood&&) = delete;

    /** @brief This node */
    const std::shared_ptr<Node> me;

    /** @brief Return true if node is in the network, false otherwise */
    bool contains(NodeId node_id)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        return nodes_.find(node_id) != nodes_.end();
    }

    /** @brief Get nodes */
    /** @return A copy of the current node map. */
    NodeMap getNodes(void)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        return nodes_;
    }

    /** @brief Get the entry for a particular node in the network */
    std::shared_ptr<Node> getNode(NodeId node_id)
    {
        std::shared_ptr<Node> node;

        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            const auto&                 [it, created] = nodes_.try_emplace(node_id, nullptr);

            // If the entry is new, construct the shared_ptr. We pass nullptr above
            // to avoid creating a shared_ptr even if the entry already exists.
            if (created) {
                node = std::make_shared<Node>(node_id);
                it->second = node;

                // Add ARP entry
                if (node_id != me->id)
                    tuntap_->addARPEntry(node_id);
            } else
                return it->second;
        }

        // We only reach this point if the node was created. We go through this
        // rigamarole so that we call new_node_callback_ without holding the
        // nodes_mutex_ mutex.
        assert(node);

        if (new_node_callback_)
            new_node_callback_(node);

        return node;
    }

    /** @brief Get the entry for a particular node in the network */
    Node& operator[](NodeId node_id)
    {
        return *getNode(node_id);
    }

    /** @brief Apply a function to each node */
    void foreach(std::function<void(Node&)> f)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);

        for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
            f(*(it->second));
    }

    /** @brief Get the node that is the time master */
    std::optional<NodeId> getTimeMaster(void);

    /** @brief Set new node callback */
    void setNewNodeCallback(const new_node_callback_t &cb)
    {
        new_node_callback_ = cb;
    }

private:
    /** @brief Our tun/tap interface */
    std::shared_ptr<TunTap> tuntap_;

    /** @brief New node callback */
    new_node_callback_t new_node_callback_;

    /** @brief Mutex protecting nodes in the network */
    std::mutex nodes_mutex_;

    /** @brief The nodes in the network */
    NodeMap nodes_;
};

#endif /* NEIGHBORHOOD_HH_ */
