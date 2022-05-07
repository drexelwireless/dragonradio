// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

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

#include "Node.hh"
#include "Seq.hh"

#if defined(DOXYGEN)
#define PACKED
#else /* !DOXYGEN */
#define PACKED __attribute__((packed))
#endif /* !DOXYGEN */

/** @brief %PHY packet header. */
struct Header {
    /** @brief Current hop. */
    NodeId curhop;

    /** @brief Next hop. */
    NodeId nexthop;

    /** @brief Packet sequence number. */
    Seq seq;

    /** @brief Packet flags. */
    struct Flags {
        /** @brief Set if the packet is the first in a new connection */
        uint8_t syn : 1;

        /** @brief Set if the packet is ACKing */
        uint8_t ack : 1;

        /** @brief Set if the packet is sequenced */
        uint8_t has_seq : 1;

        /** @brief Set if the packet has control data */
        uint8_t has_control : 1;

        /** @brief Set if the packet is compressed */
        uint8_t compressed : 1;

        /** @brief Unused flags */
        uint8_t team : 3;
    } flags;
} PACKED;

/** @brief Extended header that appears in radio payload. */
struct ExtendedHeader {
    /** @brief Source */
    NodeId src;

    /** @brief Destination */
    NodeId dest;

    /** @brief Sequence number we are ACK'ing or NAK'ing. */
    Seq ack;

    /** @brief Length of the packet payload. */
    /** The packet payload may be padded or contain control data. This field
     * gives the size of the data portion of the payload.
     */
    uint16_t data_len;
};

#endif /* HEADER_HH_ */
