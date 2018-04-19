#ifndef PACKET_HH_
#define PACKET_HH_

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "buffer.hh"
#include "Node.hh"

typedef uint16_t PacketId;

/** A packet from the network to be sent over the radio */
struct NetPacket
{
    NetPacket(size_t n) : payload(n) {};

    /** Packet ID */
    PacketId pkt_id;

    /** Source node (should be this node) */
    NodeId src;

    /** Destination node */
    NodeId dest;

    /** Payload data. */
    buffer<unsigned char> payload;
};

/** A packet from the radio to be sent over the network */
struct RadioPacket
{
    RadioPacket(unsigned char* data, size_t n) : payload(n)
    {
        memcpy(&(payload)[0], data, n);
    };

    /** Packet ID */
    PacketId pkt_id;

    /** Source node (should be this node) */
    NodeId src;

    /** Destination node */
    NodeId dest;

    /** Payload data. */
    buffer<unsigned char> payload;
};

#endif /* PACKET_HH_ */
