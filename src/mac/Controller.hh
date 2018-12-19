#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <list>

#include "net/Element.hh"
#include "net/Net.hh"

/** @brief A MAC controller. */
class Controller : public Element
{
public:
    Controller(std::shared_ptr<Net> net);
    virtual ~Controller() = default;

    Controller() = delete;

    /** @brief Pull a packet from the network to be sent next over the radio. */
    /** This function is automatically called when a packet is requested from
     * the net_out port.
     */
    virtual bool pull(std::shared_ptr<NetPacket>& pkt) = 0;

    /** @brief Process demodulated packets. */
    /** This function is automatically called to process packets received on
     * on the radio_in port.
     */
    virtual void received(std::shared_ptr<RadioPacket>&& pkt) = 0;

    /** @brief Called when net_out is disconnected. By default, this disconnects
     * net_in so that any pending pulls will terminate.
     */
    virtual void disconnect(void);

    /** @brief Notify controller that a packet has been transmitted. */
    /** This function is called by the MAC when a packet has been transmitted.
     */
    virtual void transmitted(std::shared_ptr<NetPacket>& pkt) = 0;

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
};

#endif /* CONTROLLER_H_ */
