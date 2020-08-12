#ifndef NET_PACKETCOMPRESSOR_HH_
#define NET_PACKETCOMPRESSOR_HH_

#include "Packet.hh"
#include "net/Element.hh"

/** @brief A packet compression element. */
class PacketCompressor : public Element
{
public:
    PacketCompressor() = delete;

    PacketCompressor(bool enabled = false);

    virtual ~PacketCompressor() = default;

    /** @brief Get enabled flag */
    bool getEnabled(void)
    {
        return enabled_;
    }

    /** @brief Set enabled flag */
    void setEnabled(bool enabled)
    {
        enabled_ = enabled;
    }

    /** @brief Network packet input port. */
    NetIn<Push> net_in;

    /** @brief Network packet output port. */
    NetOut<Push> net_out;

    /** @brief Radio packet input port. */
    RadioIn<Push> radio_in;

    /** @brief Radio packet output port. */
    RadioOut<Push> radio_out;

protected:
    /** @brief Is packet compression enabled? */
    bool enabled_;

    /** @brief Internal IP network */
    in_addr_t int_net_;

    /** @brief Internal IP network mask */
    in_addr_t int_netmask_;

    /** @brief External IP network */
    in_addr_t ext_net_;

    /** @brief External IP network mask */
    in_addr_t ext_netmask_;

    /** @brief Process a network packet */
    void netPush(std::shared_ptr<NetPacket> &&pkt);

    /** @brief Process a radio packet */
    void radioPush(std::shared_ptr<RadioPacket> &&pkt);

    /** @brief Compress a network packet */
    void compress(NetPacket &pkt);

    /** @brief Decompress a radio packet */
    void decompress(RadioPacket &pkt);
};

#endif /* NET_PACKETCOMPRESSOR_HH_ */
