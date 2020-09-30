// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef NETUTIL_HH_
#define NETUTIL_HH_

#include <sys/types.h>
#include <string.h>

#include "Packet.hh"

/** @brief Compute broadcast address from address and netmask  */
inline uint32_t mkBroadcastAddress(uint32_t addr, uint32_t netmask)
{
    return (addr & netmask) | (0xffffffff & ~netmask);
}

/** @brief Determine whether or not an Ethernet address is a broadcast address */
inline bool isEthernetBroadcast(const u_char *host)
{
    return memcmp(host, "\xff\xff\xff\xff\xff\xff", 6) == 0;
}

#endif /* NETUTIL_HH_ */
