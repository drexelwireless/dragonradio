#ifndef MGEN_HH_
#define MGEN_HH_

#include <sys/types.h>

/** @brief DARPA MGEN version number */
#define DARPA_MGEN_VERSION 4

/** @brief MGEN version number */
#define MGEN_VERSION 2

typedef uint64_t darpa_mgen_secs_t;

#define ntoh_darpa_mgen_secs bswap_64

typedef uint32_t mgen_secs_t;

#define ntoh_mgen_secs ntohl

typedef uint32_t mgen_usecs_t;

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
};

struct __attribute__((__packed__)) darpa_mgenhdr {
   uint16_t messageSize;
   uint8_t version;
   uint8_t flags;
   uint32_t mgenFlowId;
   uint32_t sequenceNumber;
   darpa_mgen_secs_t txTimeSeconds;
   mgen_usecs_t txTimeMicroseconds;
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
