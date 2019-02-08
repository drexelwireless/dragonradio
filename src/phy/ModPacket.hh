#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <atomic>

#include "IQBuffer.hh"
#include "Packet.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    ModPacket() : channel(0.0, 0.0), fc(0.0), incomplete ATOMIC_FLAG_INIT
    {
        incomplete.test_and_set(std::memory_order_acquire);
    };

    /** @brief Channel */
    Channel channel;

    /** @brief Ceneter frequency, which may be different from the channel's
     * center frequency if we are not upsampling on TX.
     */
    double fc;

    /** @brief Buffer containing the modulated samples. */
    std::shared_ptr<IQBuf> samples;

    /** @brief The un-modulated packet. */
    std::shared_ptr<NetPacket> pkt;

    /** @brief Flag that is set while modulation is incomplete. */
    std::atomic_flag incomplete;
};

#endif /* MODPACKET_HH_ */
