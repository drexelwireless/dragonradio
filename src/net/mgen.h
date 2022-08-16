// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MGEN_HH_
#define MGEN_HH_

#include <sys/types.h>
#include <arpa/inet.h>
#include <byteswap.h>

#include "Clock.hh"

#if defined(DOXYGEN)
#define PACKED
#else /* !DOXYGEN */
#define PACKED __attribute__((packed))
#endif /* !DOXYGEN */

enum {
    /** @brief MGEN version number */
    MGEN_VERSION = 2,

    /** @brief DARPA MGEN version number */
    DARPA_MGEN_VERSION = 4
};

enum {
    MGEN_INVALID_ADDRESS = 0,
    MGEN_IPv4            = 1,
    MGEN_IPv6            = 2
};

enum
{
    MGEN_INVALID_GPS = 0,
    MGEN_STALE       = 1,
    MGEN_CURRENT     = 2
};

enum {
    MGEN_CLEAR          = 0x00,
    MGEN_CONTINUES      = 0x01,
    MGEN_END_OF_MSG     = 0x02,
    MGEN_CHECKSUM       = 0x04,
    MGEN_LAST_BUFFER    = 0x08,
    MGEN_CHECKSUM_ERROR = 0x10
};

typedef uint32_t mgen_secs_t;

typedef uint32_t mgen_usecs_t;

/** @brief A DARPA-variant MGEN packet header */
struct PACKED darpa_mgenhdr {
   uint16_t messageSize;
   uint8_t version;
   uint8_t flags;
   uint32_t mgenFlowId;
   uint32_t sequenceNumber;
   uint32_t reserved;
   mgen_secs_t txTimeSeconds;
   mgen_usecs_t txTimeMicroseconds;
};

/** @brief An MGEN packet header */
/** See:
 * https://downloads.pf.itd.nrl.navy.mil/docs/mgen/mgen.html
 */
struct PACKED mgenhdr {
    uint16_t messageSize;
    uint8_t version;
    uint8_t flags;
    uint32_t mgenFlowId;
    uint32_t sequenceNumber;
    mgen_secs_t txTimeSeconds;
    mgen_usecs_t txTimeMicroseconds;

    uint16_t getMessageSize(void) const
    {
        uint16_t temp;

        std::memcpy(&temp, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, messageSize), sizeof(temp));

        return ntohs(temp);
    }

    uint32_t getFlowId(void) const
    {
        uint32_t temp;

        std::memcpy(&temp, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, mgenFlowId), sizeof(temp));

        return ntohl(temp);
    }

    uint32_t getSequenceNumber(void) const
    {
        uint32_t temp;

        std::memcpy(&temp, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, sequenceNumber), sizeof(temp));

        return ntohl(temp);
    }

    WallClock::time_point getTimestamp(void) const
    {
        mgen_secs_t  hdr_secs;
        mgen_usecs_t hdr_usecs;
        int64_t      secs;
        int32_t      usecs;

        if (version == DARPA_MGEN_VERSION) {
            const struct darpa_mgenhdr *dmgenh = reinterpret_cast<const struct darpa_mgenhdr*>(this);

            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));
        } else {
            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));
        }

        secs = ntohl(hdr_secs);
        usecs = ntohl(hdr_usecs);

        std::chrono::duration<timerep_t> dur(timerep_t(secs, usecs/1e6));

        return WallClock::time_point{dur};
    }
};

struct PACKED mgenaddr {
    uint16_t dstPort;
    uint8_t dstAddrType;
    uint8_t dstAddrLen;
};

struct PACKED mgenstdaddr {
    uint16_t dstPort;
    uint8_t dstAddrType;
    uint8_t dstAddrLen;
    u_int32_t dstIPAddr;
    uint16_t hostPort;
    uint8_t hostAddrType;
    uint8_t hostAddrLen;
};

struct PACKED mgenrest {
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
    uint8_t gpsStatus;
    uint8_t reserved;
    uint16_t payloadLen;
};

struct PACKED darpa_mgenrest {
    int8_t tos;
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
    uint8_t gpsStatus;
    uint8_t reserved;
    uint16_t payloadLen;
};

#endif /* MGEN_HH_ */
