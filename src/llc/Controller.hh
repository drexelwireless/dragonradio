// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <chrono>
#include <functional>
#include <list>

using namespace std::chrono_literals;
using namespace std::placeholders;

#include "Neighborhood.hh"
#include "Node.hh"
#include "net/Element.hh"
#include "net/Queue.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief LLC's link to the network */
class ControllerNetLink {
public:
    ControllerNetLink() = default;
    virtual ~ControllerNetLink() = default;

    /** @brief Push an element onto the high-priority queue. */
    virtual void push_hi(std::shared_ptr<NetPacket>&& pkt) = 0;

    /** @brief Re-queue an element. */
    virtual void repush(std::shared_ptr<NetPacket>&& pkt)
    {
        push_hi(std::move(pkt));
    }

    /** @brief Notify queue of new node metric
     * @param id Node whose metric has changed.
     * @param metric The new node metric. A larger value indicates a better
     * metric.
     */
    virtual void updateMetric(NodeId id, double metric)
    {
    }

    /** @brief Set transmission delay. */
    virtual void setTransmissionDelay(std::chrono::duration<double> t)
    {
    }

    /** @brief Get transmission delay. */
    virtual std::chrono::duration<double> getTransmissionDelay(void) const
    {
        return 0.0s;
    }

    /** @brief Set whether or not a node's link is open */
    virtual void setLinkStatus(NodeId node_id, bool isOpen)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        link_status_[node_id] = isOpen;
    }

protected:
    /** @brief Mutex protecting the send window status */
    std::mutex mutex_;

    /** @brief Nodes' send window statuses */
    std::unordered_map<NodeId, bool> link_status_;

    /** @brief Return true if the packet can be popped */
    bool canPop(const std::shared_ptr<NetPacket>& pkt)
    {
        if (pkt->hdr.nexthop == kNodeBroadcast || pkt->internal_flags.assigned_seq)
            return true;

        std::lock_guard<std::mutex> lock(mutex_);
        auto                        it = link_status_.find(pkt->hdr.nexthop);

        if (it != link_status_.end())
            return it->second;
        else
            return true;
    }
};

/** @brief A logical link controller. */
class Controller : public Element
{
public:
    Controller(std::shared_ptr<Neighborhood> nhood,
               size_t mtu)
      : net_in(*this, nullptr, nullptr)
      , net_out(*this,
                nullptr,
                std::bind(&Controller::disconnect, this),
                std::bind(&Controller::pull, this, _1),
                std::bind(&Controller::kick, this))
      , radio_in(*this,nullptr, nullptr,
                 std::bind(&Controller::received, this, _1))
      , radio_out(*this, nullptr, nullptr)
      , nhood_(nhood)
      , netlink_(nullptr)
      , mtu_(mtu)
    {
    }
    virtual ~Controller() = default;

    Controller() = delete;

    /** @brief Get MTU */
    size_t getMTU() const
    {
        return mtu_;
    }

    /** @brief Set channels */
    virtual void setChannels(const std::vector<PHYChannel> &channels)
    {
    }

    /** @brief Get the controller's network link. */
    std::shared_ptr<ControllerNetLink> getNetLink(void)
    {
        return netlink_;
    }

    /** @brief Set the controller's network link. */
    void setNetLink(std::shared_ptr<ControllerNetLink> netlink)
    {
        netlink_ = netlink;
    }

    /** @brief Set whether or not node is subject to emissions control. */
    virtual void setEmcon(NodeId node_id, bool emcon)
    {
        (*nhood_)[node_id].emcon = emcon;
    }

    /** @brief Pull a packet from the network to be sent next over the radio. */
    /** This function is automatically called when a packet is requested from
     * the net_out port.
     */
    virtual bool pull(std::shared_ptr<NetPacket> &pkt) = 0;

    /** @brief Kick the controller. */
    virtual void kick(void)
    {
        net_in.kick();
    }

    /** @brief Process demodulated packets. */
    /** This function is automatically called to process packets received on
     * on the radio_in port.
     */
    virtual void received(std::shared_ptr<RadioPacket> &&pkt) = 0;

    /** @brief Called when net_out is disconnected. By default, this disconnects
     * net_in so that any pending pulls will terminate.
     */
    virtual void disconnect(void)
    {
        net_in.disconnect();
    }

    /** @brief Notify controller that a packet missed its transmission slot. */
    /** This function is called by the MAC when a packet has missed its
     * transmission slot.
     */
    virtual void missed(std::shared_ptr<NetPacket> &&pkt)
    {
        netlink_->repush(std::move(pkt));
    }

    /** @brief Notify controller of transmitted packets. */
    virtual void transmitted(std::list<std::unique_ptr<ModPacket>> &mpkts)
    {
    }

    /** @brief Input port for packets coming from the network. */
    NetIn<Pull> net_in;

    /** @brief Output port for network packets processed by the controller */
    NetOut<Pull> net_out;

    /** @brief Input port for demodulated packets coming from the radio. */
    RadioIn<Push> radio_in;

    /** @brief Output port for demodulated packets processed by the controller */
    RadioOut<Push> radio_out;

protected:
    /** @brief The Net we're attached to */
    std::shared_ptr<Neighborhood> nhood_;

    /** @brief Network queue with high-priority sub-queue. */
    std::shared_ptr<ControllerNetLink> netlink_;

    /** @brief Network MTU */
    size_t mtu_;
};

#endif /* CONTROLLER_H_ */
