#ifndef MODPACKET_HH_
#define MODPACKET_HH_

#include <atomic>

#include "IQBuffer.hh"
#include "Packet.hh"

/** A modulated data packet to be sent over the radio */
struct ModPacket
{
    ModPacket() : fc(0), complete ATOMIC_FLAG_INIT
    {
        complete.test_and_set(std::memory_order_acquire);
    };

    /** @brief Center frequency. */
    float fc;

    /** @brief Buffer containing the modulated samples. */
    std::shared_ptr<IQBuf> samples;

    /** @brief The un-modulated packet. */
    std::shared_ptr<NetPacket> pkt;

    /** @brief Flag that is set until modulation is completed. */
    std::atomic_flag complete;
};

#endif /* MODPACKET_HH_ */
