#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <atomic>

#include "IQBuffer.hh"
#include "Packet.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    /** @brief Channel */
    Channel channel;

    /** @brief Offset of start of packet, in number of samples */
    size_t start;

    /** @brief Number of modulated samples */
    size_t nsamples;

    /** @brief Buffer containing the modulated samples. */
    std::shared_ptr<IQBuf> samples;

    /** @brief The un-modulated packet. */
    std::shared_ptr<NetPacket> pkt;
};

#endif /* MODPACKET_HH_ */
