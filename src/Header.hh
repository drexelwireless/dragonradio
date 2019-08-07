#ifndef HEADER_HH_
#define HEADER_HH_

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>

#include <complex>
#include <cstddef>
#include <iterator>
#include <vector>

#include <liquid/liquid.h>

#include "Seq.hh"

typedef uint8_t NodeId;

struct PacketFlags {
    /** @brief Set if the packet is the first in a new connection */
    uint16_t syn : 1;

    /** @brief Set if the packet is ACKing */
    uint16_t ack : 1;

    /** @brief Set if this is a broadcast packet */
    uint16_t broadcast : 1;

    /** @brief Set if the packet has control data */
    uint16_t control : 1;

    /** @brief Unused flags */
    uint16_t unused : 12;
};

enum {
    /** @brief Set if the packet has an assigned sequence number */
    kHasSeq = 0,

    /** @brief Set if the packet belongs to the internal IP network (10.*) */
    kIntNet,

    /** @brief Set if the packet belongs to the external IP network (192.168.*) */
    kExtNet,

    /** @brief Set if the packet had invalid header */
    kInvalidHeader,

    /** @brief Set if the packet had invalid payload */
    kInvalidPayload,

    /** @brief Set if the packet is a retransmission */
    kRetransmission,

    /** @brief Set if the packet contains a selective ACK */
    kHasSelectiveACK,

    /** @brief Set if this is a timestamp packet */
    kIsTimestamp
};

typedef uint16_t InternalFlags;

/** @brief %PHY packet header. */
struct Header {
    /** @brief Current hop. */
    NodeId curhop;

    /** @brief Next hop. */
    NodeId nexthop;

    /** @brief Packet flags. */
    PacketFlags flags;

    /** @brief Packet sequence number. */
    Seq seq;

    /** @brief Length of the packet payload. */
    /** The packet payload may be padded or contain control data. This field
     * gives the size of the data portion of the payload.
     */
    uint16_t data_len;
};

/** @brief Extended header that appears in radio payload. */
struct ExtendedHeader {
    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief Sequence number we are ACK'ing or NAK'ing. */
    Seq ack;
};

#endif /* HEADER_HH_ */
