// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <functional>
#include <list>

using namespace std::placeholders;

#include "net/Element.hh"
#include "net/Net.hh"
#include "net/Queue.hh"
#include "phy/PHY.hh"
#include "phy/Synthesizer.hh"

/** @brief A MAC controller. */
class Controller : public Element
{
public:
    Controller(std::shared_ptr<Net> net)
      : net_in(*this, nullptr, nullptr)
      , net_out(*this,
                nullptr,
                std::bind(&Controller::disconnect, this),
                std::bind(&Controller::pull, this, _1),
                std::bind(&Controller::kick, this))
      , radio_in(*this,nullptr, nullptr,
                 std::bind(&Controller::received, this, _1))
      , radio_out(*this, nullptr, nullptr)
      , net_(net)
      , netq_(nullptr)
      , min_channel_bandwidth_(0)
    {
    }
    virtual ~Controller() = default;

    Controller() = delete;

    /** @brief Get the controller's network queue. */
    std::shared_ptr<NetQueue> getNetQueue(void)
    {
        return netq_;
    }

    /** @brief Set the controller's network queue. */
    void setNetQueue(std::shared_ptr<NetQueue> q)
    {
        netq_ = q;
    }

    /** @brief Set minimum channel bandwidth */
    virtual void setMinChannelBandwidth(double min_bw)
    {
        min_channel_bandwidth_ = min_bw;
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
        netq_->repush(std::move(pkt));
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
    std::shared_ptr<Net> net_;

    /** @brief Network queue with high-priority sub-queue. */
    std::shared_ptr<NetQueue> netq_;

    /** @brief Bandwidth of the smallest channel */
    double min_channel_bandwidth_;
};

#endif /* CONTROLLER_H_ */
