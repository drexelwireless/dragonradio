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
    virtual size_t getLowWaterMark(void) = 0;

    /** @brief Set the low-water mark, i.e., the minimum number of samples we
     * want to have available at all times.
     */
    virtual void setLowWaterMark(size_t mark) = 0;

    /** @brief Pop a list of modulated packet such that the total number of
     * modulated samples is maxSamples or fewer.
     * @param pkts A reference to a list to which the popped packets will be
     * appended.
     * @param maxSample The maximum number of samples to pop.
     */
    virtual void pop(std::list<std::unique_ptr<ModPacket>>& pkts, size_t maxSamples) = 0;
};

#endif /* PACKETMODULATOR_H_ */
