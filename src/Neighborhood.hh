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
#include <set>
#include <thread>
#include <vector>

#include "Node.hh"
#include "Packet.hh"
#include "WorkQueue.hh"
#include "net/TunTap.hh"

/** @brief A listener for neighborhood events */
class NeighborhoodListener
{
public:
    NeighborhoodListener() = default;

    virtual ~NeighborhoodListener() = default;

    /** @brief Called when a neighbor is added
     * @param neighbor The neighbor that was added
     * */
    virtual void neighborAdded(const std::shared_ptr<Node> &neighbor)
    {
    }

    /** @brief Called when a neighbor is removed
     * @param neighbor The neighbor that was removed
     * */
    virtual void neighborRemoved(const std::shared_ptr<Node> &neighbor)
    {
    }

    /** @brief Called when a gateway is added
     * @param neighbor The gateway that was added
     * */
    virtual void gatewayAdded(const std::shared_ptr<Node> &neighbor)
    {
    }
};

/** @brief The local (one-hop) neighborhood */
class Neighborhood
{
public:
    using NodeMap = std::map<NodeId, std::shared_ptr<Node>>;

    Neighborhood() = delete;

    Neighborhood(std::shared_ptr<TunTap> tuntap,
                 NodeId this_node_id)
      : me(std::make_shared<Node>(this_node_id))
      , tuntap_(tuntap)
      , neighbors_({ {this_node_id, me} })
    {
    }

    ~Neighborhood() = default;

    Neighborhood(const Neighborhood&) = delete;
    Neighborhood(Neighborhood&&) = delete;

    Neighborhood& operator=(const Neighborhood&) = delete;
    Neighborhood& operator=(Neighborhood&&) = delete;

    /** @brief This node */
    const std::shared_ptr<Node> me;

    /** @brief Get the node that is the time master */
    std::optional<NodeId> getTimeMaster(void) const;

    /** @brief Return true if node is in the neighborhood, false otherwise */
    bool contains(NodeId node_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return neighbors_.find(node_id) != neighbors_.end();
    }

    /** @brief Get one-hop neighbors */
    /** @return The current one-hop neighbors. */
    NodeMap getNeighbors(void) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return neighbors_;
    }

    /** @brief Get the entry for a particular node in the network */
    const std::shared_ptr<Node> &operator[](NodeId node_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return neighbors_.at(node_id);
    }

    /** @brief Apply a function to each neighbor */
    template <typename F>
    void foreach(F&& f)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = neighbors_.begin(); it != neighbors_.end(); ++it)
            f(*(it->second));
    }

    /** @brief Apply a function to each neighbor */
    template <typename F>
    void foreach(F&& f) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto it = neighbors_.begin(); it != neighbors_.end(); ++it)
            f(*(it->second));
    }

    /** @brief Add a one-hop neighbor
     * @param node_id The node id of the neighbor to add
     * @return The new neighbor node.
     */
    std::shared_ptr<Node> addNeighbor(NodeId node_id);

    /** @brief Add a one-hop neighbor
     * @param node The neighbor node
     */
    void addNeighbor(const std::shared_ptr<Node> &node);

    /** @brief Remove a one-hop neighbor
     * @param node_id The node id of the neighbor to remove
     */
    void removeNeighbor(NodeId node_id);

    /** @brief Add a gateway
     * @param node The gateway node
     */
    void addGateway(const std::shared_ptr<Node> &node);

    /** @brief Add a listener
     * @param listener The listener to add
     */
    void addListener(const std::shared_ptr<NeighborhoodListener> &listener)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        listeners_.insert(listener);
    }

    /** @brief Remove a listener
     * @param listener The listener to remove
     */
    void removeListener(const std::shared_ptr<NeighborhoodListener> &listener)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = listeners_.find(listener);

        if (it != listeners_.end())
            listeners_.erase(it);
    }

private:
    /** @brief Our tun/tap interface */
    std::shared_ptr<TunTap> tuntap_;

    /** @brief Mutex protecting the neighborhood */
    mutable std::mutex mutex_;

    /** @brief The one-hop neighbors */
    NodeMap neighbors_;

    /** @brief Listeners */
    std::set<std::shared_ptr<NeighborhoodListener>> listeners_;

    /** @brief Apply a notification function to each listener */
    template <typename F>
    void notify(F&& f) const
    {
        std::set<std::shared_ptr<NeighborhoodListener>> listeners;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            listeners = listeners_;
        }

        work_queue.submit([listeners=std::move(listeners), f]()
                          {
                              for (auto&& it : listeners)
                                  f(*it);
                          });
    }
};

#endif /* NEIGHBORHOOD_HH_ */
