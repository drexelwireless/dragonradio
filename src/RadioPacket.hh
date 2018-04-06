#ifndef RADIOPACKET_HH_
#define RADIOPACKET_HH_

#include <sys/types.h>
#include <stdint.h>

#include <vector>

#include "Node.hh"

typedef uint16_t PacketId;

/** A data packet to be sent over the radio */
struct RadioPacket
{
    RadioPacket(size_t n) : payload(n) {};

    PacketId                   packet_id;
    NodeId                     dest;
    std::vector<unsigned char> payload;
    size_t                     payload_len;
};

#endif /* RADIOPACKET_HH_ */
