#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <sys/types.h>

#include "IQBuffer.hh"
#include "Packet.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    /** @brief Buffer containing the modulated samples. */
    std::shared_ptr<IQBuf> samples;

    /** @brief The un-modulated packet. */
    std::unique_ptr<NetPacket> pkt;
};

#endif /* MODPACKET_HH_ */
