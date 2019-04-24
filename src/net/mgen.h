#ifndef MGEN_HH_
#define MGEN_HH_

#include <sys/types.h>
#include <arpa/inet.h>
#include <byteswap.h>

#include "Clock.hh"

/** @brief DARPA MGEN version number */
#define DARPA_MGEN_VERSION 4

/** @brief MGEN version number */
#define MGEN_VERSION 2

typedef uint64_t darpa_mgen_secs_t;

#define ntoh_darpa_mgen_secs bswap_64

typedef uint32_t mgen_secs_t;

#define ntoh_mgen_secs ntohl

typedef uint32_t mgen_usecs_t;

/** @brief A DARPA-variant MGEN packet header */
struct __attribute__((__packed__)) darpa_mgenhdr {
   uint16_t messageSize;
   uint8_t version;
   uint8_t flags;
   uint32_t mgenFlowId;
   uint32_t sequenceNumber;
   darpa_mgen_secs_t txTimeSeconds;
   mgen_usecs_t txTimeMicroseconds;
};

/** @brief An MGEN packet header */
/** See:
 * https://downloads.pf.itd.nrl.navy.mil/docs/mgen/mgen.html
 */
struct __attribute__((__packed__)) mgenhdr {
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

    Clock::time_point getTimestamp(void) const
    {
        int64_t secs;
        int32_t usecs;

        if (version == DARPA_MGEN_VERSION) {
            const struct darpa_mgenhdr *dmgenh = reinterpret_cast<const struct darpa_mgenhdr*>(this);
            darpa_mgen_secs_t          hdr_secs;
            mgen_usecs_t               hdr_usecs;

            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(dmgenh) + offsetof(struct darpa_mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));

            secs = ntoh_darpa_mgen_secs(hdr_secs);
            usecs = ntohl(hdr_usecs);
        } else {
            mgen_secs_t  hdr_secs;
            mgen_usecs_t hdr_usecs;

            std::memcpy(&hdr_secs, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, txTimeSeconds), sizeof(hdr_secs));
            std::memcpy(&hdr_usecs, reinterpret_cast<const char*>(this) + offsetof(struct mgenhdr, txTimeMicroseconds), sizeof(hdr_usecs));

            secs = ntoh_mgen_secs(hdr_secs);
            usecs = ntohl(hdr_usecs);
        }

        return Clock::time_point{static_cast<int64_t>(secs), usecs/1e6};
    }
};

struct __attribute__((__packed__)) mgenaddr {
    uint16_t dstPort;
    uint8_t dstAddrType;
    uint8_t dstAddrLen;
};

struct __attribute__((__packed__)) mgenrest {
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
    uint8_t gpsStatus;
    uint8_t reserved;
    uint16_t payloadLen;
};

struct __attribute__((__packed__)) darpa_mgenrest {
    int8_t tos;
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
    uint8_t gpsStatus;
    uint8_t reserved;
    uint16_t payloadLen;
};

#endif /* MGEN_HH_ */
