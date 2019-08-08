#ifndef NET_PACKETCOMPRESSOR_HH_
#define NET_PACKETCOMPRESSOR_HH_

#include "Packet.hh"
#include "net/Element.hh"

/** @brief A packet compression element. */
class PacketCompressor : public Element
{
public:
    PacketCompressor();

    virtual ~PacketCompressor() = default;

    /** @brief Network packet input port. */
    NetIn<Push> net_in;

    /** @brief Network packet output port. */
    NetOut<Push> net_out;

    /** @brief Radio packet input port. */
    RadioIn<Push> radio_in;

    /** @brief Radio packet output port. */
    RadioOut<Push> radio_out;

protected:
    /** @brief Handle a network packet */
    void netPush(std::shared_ptr<NetPacket> &&pkt);

    /** @brief Handle a radio packet */
    void radioPush(std::shared_ptr<RadioPacket> &&pkt);
};

#endif /* NET_PACKETCOMPRESSOR_HH_ */
