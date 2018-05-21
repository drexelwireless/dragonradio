#ifndef PACKET_HH_
#define PACKET_HH_

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include <liquid/liquid.h>

#include "buffer.hh"

typedef uint8_t NodeId;

typedef uint16_t PacketFlags;

typedef uint16_t Seq;

/** @brief %PHY packet header. */
struct Header {
    /** @brief Current hop. */
    NodeId curhop;

    /** @brief Next hop. */
    NodeId nexthop;

    /** @brief Packet sequence number. */
    Seq seq;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Length of the packet payload. */
    /** The packet payload may be padded or contain control data. This field
     * gives the size of the data portion of the payload.
     */
    uint16_t data_len;
};

/** @brief A packet recevied from the network. */
struct NetPacket : public buffer<unsigned char>
{
    NetPacket(size_t n) : buffer(n) {};

    /** @brief Current hop (should be this node) */
    NodeId curhop;

    /** @brief Next hop */
    NodeId nexthop;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Sequence number */
    Seq seq;

    /** @brief Length of data portion of the packet */
    uint16_t data_len;

    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief CRC */
    crc_scheme check;

    /** @brief FEC0 (inner FEC) */
    fec_scheme fec0;

    /** @brief FEC1 (outer FEC) */
    fec_scheme fec1;

    /** @brief Modulation scheme */
    modulation_scheme ms;

    /** @brief Soft TX gain */
    float g;
};

/** @brief A packet received from the radio. */
struct RadioPacket : public buffer<unsigned char>
{
    RadioPacket() : buffer(), barrier(false) {};

    RadioPacket(unsigned char* data, size_t n) : buffer(data, n), barrier(false) {}

    /** @brief Current hop */
    NodeId curhop;

    /** @brief Next hop (should be this node) */
    NodeId nexthop;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Packet sequence number */
    Seq seq;

    /** @brief Length of data portion of the packet */
    uint16_t data_len;

    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief Error vector magnitude [dB] */
    float evm;

    /** @brief Received signal strength indicator [dB] */
    float rssi;

    /** @brief This Boolean is true if this packet is a barrier and should not
     * be processed or removed from a queue except by its creator.
     */
    bool barrier;
};

#endif /* PACKET_HH_ */
