#ifndef PACKETMODULATOR_H_
#define PACKETMODULATOR_H_

#include "ModPacket.hh"

/** @brief A packet modulator. */
class PacketModulator
{
public:
    PacketModulator() {};
    virtual ~PacketModulator() {};

    /** @brief Get the low-water mark. */
    virtual size_t getWatermark(void) = 0;

    /** @brief Set the low-water mark, i.e., the minimum number of samples we
     * want to have available at all times.
     */
    virtual void setWatermark(size_t watermark) = 0;

    /** @brief Pop a modulated packet, but only if it consist of maxSamples
     * samples or fewer.
     */
    virtual std::unique_ptr<ModPacket> pop(size_t maxSamples) = 0;
};

#endif /* PACKETMODULATOR_H_ */
