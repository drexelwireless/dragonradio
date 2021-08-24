// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef RADIONET_HH_
#define RADIONET_HH_

#include <math.h>

#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "Packet.hh"
#include "net/TunTap.hh"

struct GPSLocation {
    GPSLocation() : lat(0.0), lon(0.0), alt(0.0), timestamp(0.0)
    {
    }

    /** @brief Latitude */
    double lat;

    /** @brief Longitude */
    double lon;

    /** @brief Altitude */
    double alt;

    /** @brief Timestamp of last update */
    double timestamp;
};

/** @brief Map from timestamp sequence number to timestamp. */
using timestamp_map = std::unordered_map<TimestampSeq, MonoClock::time_point>;

/** @brief Map from timestamp sequence number to pair of sent, received timestamps. */
using timestamps_map = std::unordered_map<TimestampSeq, std::pair<MonoClock::time_point, MonoClock::time_point>>;

/** @brief Vector of pairs of timestamps. */
using timestampseq_set = std::unordered_set<TimestampSeq>;

struct Node {
    explicit Node(NodeId id)
      : id(id)
      , is_gateway(false)
      , can_transmit(true)
      , g(1.0)
      , mcsidx(0)
    {
    }

    Node() = delete;
    Node(const Node &) = delete;

    ~Node() = default;

    /** @brief Node ID */
    const NodeId id;

    /** @brief Location */
    GPSLocation loc;

    /** @brief Flag indicating whether or not this node is the gateway */
    bool is_gateway;

    /** @brief Flag indicating whether or not this node can transmit */
    bool can_transmit;

    /** @brief Multiplicative TX gain as measured against 0 dBFS. */
    float g;

    /** @brief MCS for this node */
    mcsidx_t mcsidx;

    /** @brief Set soft TX gain.
     * @param dB The soft gain (dBFS).
     */
    void setSoftTXGain(float dB)
    {
        g = powf(10.0f, dB/20.0f);
    }

    /** @brief Get soft TX gain (dBFS). */
    float getSoftTXGain(void) const
    {
        return 20.0*logf(g)/logf(10.0);
    }
};

class RadioNet
{
public:
    using NodeMap = std::map<NodeId, std::shared_ptr<Node>>;

    using new_node_callback_t = std::function<void(const std::shared_ptr<Node>&)>;

    RadioNet() = delete;

    RadioNet(std::shared_ptr<TunTap> tuntap,
             NodeId this_node_id)
      : tuntap_(tuntap)
      , this_node_id_(this_node_id)
      , this_node_(std::make_shared<Node>(this_node_id))
      , nodes_({ {this_node_id_, this_node_} })
    {
    }

    ~RadioNet() = default;

    RadioNet(const RadioNet&) = delete;
    RadioNet(RadioNet&&) = delete;

    RadioNet& operator=(const RadioNet&) = delete;
    RadioNet& operator=(RadioNet&&) = delete;

    /** @brief Get this node's ID */
    inline NodeId getThisNodeId(void) const
    {
        return this_node_id_;
    }

    /** @brief Get the entry for this node */
    inline Node& getThisNode(void)
    {
        return *this_node_;
    }

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
                if (node_id != this_node_id_)
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

    /** @brief This node's ID */
    const NodeId this_node_id_;

    /** @brief This node */
    std::shared_ptr<Node> this_node_;

    /** @brief New node callback */
    new_node_callback_t new_node_callback_;

    /** @brief Mutex protecting nodes in the network */
    std::mutex nodes_mutex_;

    /** @brief The nodes in the network */
    NodeMap nodes_;
};

#endif /* RADIONET_HH_ */
